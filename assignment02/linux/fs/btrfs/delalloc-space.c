// SPDX-License-Identifier: GPL-2.0

#include "ctree.h"
#include "delalloc-space.h"
#include "block-rsv.h"
#include "btrfs_inode.h"
#include "space-info.h"
#include "transaction.h"
#include "qgroup.h"
#include "block-group.h"

/*
 * HOW DOES THIS WORK
 *
 * There are two stages to data reservations, one for data and one for metadata
 * to handle the new extents and checksums generated by writing data.
 *
 *
 * DATA RESERVATION
 *   The general flow of the data reservation is as follows
 *
 *   -> Reserve
 *     We call into btrfs_reserve_data_bytes() for the user request bytes that
 *     they wish to write.  We make this reservation and add it to
 *     space_info->bytes_may_use.  We set EXTENT_DELALLOC on the inode io_tree
 *     for the range and carry on if this is buffered, or follow up trying to
 *     make a real allocation if we are pre-allocating or doing O_DIRECT.
 *
 *   -> Use
 *     At writepages()/prealloc/O_DIRECT time we will call into
 *     btrfs_reserve_extent() for some part or all of this range of bytes.  We
 *     will make the allocation and subtract space_info->bytes_may_use by the
 *     original requested length and increase the space_info->bytes_reserved by
 *     the allocated length.  This distinction is important because compression
 *     may allocate a smaller on disk extent than we previously reserved.
 *
 *   -> Allocation
 *     finish_ordered_io() will insert the new file extent item for this range,
 *     and then add a delayed ref update for the extent tree.  Once that delayed
 *     ref is written the extent size is subtracted from
 *     space_info->bytes_reserved and added to space_info->bytes_used.
 *
 *   Error handling
 *
 *   -> By the reservation maker
 *     This is the simplest case, we haven't completed our operation and we know
 *     how much we reserved, we can simply call
 *     btrfs_free_reserved_data_space*() and it will be removed from
 *     space_info->bytes_may_use.
 *
 *   -> After the reservation has been made, but before cow_file_range()
 *     This is specifically for the delalloc case.  You must clear
 *     EXTENT_DELALLOC with the EXTENT_CLEAR_DATA_RESV bit, and the range will
 *     be subtracted from space_info->bytes_may_use.
 *
 * METADATA RESERVATION
 *   The general metadata reservation lifetimes are discussed elsewhere, this
 *   will just focus on how it is used for delalloc space.
 *
 *   We keep track of two things on a per inode bases
 *
 *   ->outstanding_extents
 *     This is the number of file extent items we'll need to handle all of the
 *     outstanding DELALLOC space we have in this inode.  We limit the maximum
 *     size of an extent, so a large contiguous dirty area may require more than
 *     one outstanding_extent, which is why count_max_extents() is used to
 *     determine how many outstanding_extents get added.
 *
 *   ->csum_bytes
 *     This is essentially how many dirty bytes we have for this inode, so we
 *     can calculate the number of checksum items we would have to add in order
 *     to checksum our outstanding data.
 *
 *   We keep a per-inode block_rsv in order to make it easier to keep track of
 *   our reservation.  We use btrfs_calculate_inode_block_rsv_size() to
 *   calculate the current theoretical maximum reservation we would need for the
 *   metadata for this inode.  We call this and then adjust our reservation as
 *   necessary, either by attempting to reserve more space, or freeing up excess
 *   space.
 *
 * OUTSTANDING_EXTENTS HANDLING
 *
 *  ->outstanding_extents is used for keeping track of how many extents we will
 *  need to use for this inode, and it will fluctuate depending on where you are
 *  in the life cycle of the dirty data.  Consider the following normal case for
 *  a completely clean inode, with a num_bytes < our maximum allowed extent size
 *
 *  -> reserve
 *    ->outstanding_extents += 1 (current value is 1)
 *
 *  -> set_delalloc
 *    ->outstanding_extents += 1 (currrent value is 2)
 *
 *  -> btrfs_delalloc_release_extents()
 *    ->outstanding_extents -= 1 (current value is 1)
 *
 *    We must call this once we are done, as we hold our reservation for the
 *    duration of our operation, and then assume set_delalloc will update the
 *    counter appropriately.
 *
 *  -> add ordered extent
 *    ->outstanding_extents += 1 (current value is 2)
 *
 *  -> btrfs_clear_delalloc_extent
 *    ->outstanding_extents -= 1 (current value is 1)
 *
 *  -> finish_ordered_io/btrfs_remove_ordered_extent
 *    ->outstanding_extents -= 1 (current value is 0)
 *
 *  Each stage is responsible for their own accounting of the extent, thus
 *  making error handling and cleanup easier.
 */

int btrfs_alloc_data_chunk_ondemand(struct btrfs_inode *inode, u64 bytes)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_FLUSH_DATA;

	/* Make sure bytes are sectorsize aligned */
	bytes = ALIGN(bytes, fs_info->sectorsize);

	if (btrfs_is_free_space_inode(inode))
		flush = BTRFS_RESERVE_FLUSH_FREE_SPACE_INODE;

	return btrfs_reserve_data_bytes(fs_info, bytes, flush);
}

int btrfs_check_data_free_space(struct btrfs_inode *inode,
			struct extent_changeset **reserved, u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	int ret;

	/* align the range */
	len = round_up(start + len, fs_info->sectorsize) -
	      round_down(start, fs_info->sectorsize);
	start = round_down(start, fs_info->sectorsize);

	ret = btrfs_alloc_data_chunk_ondemand(inode, len);
	if (ret < 0)
		return ret;

	/* Use new btrfs_qgroup_reserve_data to reserve precious data space. */
	ret = btrfs_qgroup_reserve_data(inode, reserved, start, len);
	if (ret < 0)
		btrfs_free_reserved_data_space_noquota(fs_info, len);
	else
		ret = 0;
	return ret;
}

/*
 * Called if we need to clear a data reservation for this inode
 * Normally in a error case.
 *
 * This one will *NOT* use accurate qgroup reserved space API, just for case
 * which we can't sleep and is sure it won't affect qgroup reserved space.
 * Like clear_bit_hook().
 */
void btrfs_free_reserved_data_space_noquota(struct btrfs_fs_info *fs_info,
					    u64 len)
{
	struct btrfs_space_info *data_sinfo;

	ASSERT(IS_ALIGNED(len, fs_info->sectorsize));

	data_sinfo = fs_info->data_sinfo;
	btrfs_space_info_free_bytes_may_use(fs_info, data_sinfo, len);
}

/*
 * Called if we need to clear a data reservation for this inode
 * Normally in a error case.
 *
 * This one will handle the per-inode data rsv map for accurate reserved
 * space framework.
 */
void btrfs_free_reserved_data_space(struct btrfs_inode *inode,
			struct extent_changeset *reserved, u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	/* Make sure the range is aligned to sectorsize */
	len = round_up(start + len, fs_info->sectorsize) -
	      round_down(start, fs_info->sectorsize);
	start = round_down(start, fs_info->sectorsize);

	btrfs_free_reserved_data_space_noquota(fs_info, len);
	btrfs_qgroup_free_data(inode, reserved, start, len);
}

/**
 * btrfs_inode_rsv_release - release any excessive reservation.
 * @inode - the inode we need to release from.
 * @qgroup_free - free or convert qgroup meta.
 *   Unlike normal operation, qgroup meta reservation needs to know if we are
 *   freeing qgroup reservation or just converting it into per-trans.  Normally
 *   @qgroup_free is true for error handling, and false for normal release.
 *
 * This is the same as btrfs_block_rsv_release, except that it handles the
 * tracepoint for the reservation.
 */
static void btrfs_inode_rsv_release(struct btrfs_inode *inode, bool qgroup_free)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_block_rsv *block_rsv = &inode->block_rsv;
	u64 released = 0;
	u64 qgroup_to_release = 0;

	/*
	 * Since we statically set the block_rsv->size we just want to say we
	 * are releasing 0 bytes, and then we'll just get the reservation over
	 * the size free'd.
	 */
	released = btrfs_block_rsv_release(fs_info, block_rsv, 0,
					   &qgroup_to_release);
	if (released > 0)
		trace_btrfs_space_reservation(fs_info, "delalloc",
					      btrfs_ino(inode), released, 0);
	if (qgroup_free)
		btrfs_qgroup_free_meta_prealloc(inode->root, qgroup_to_release);
	else
		btrfs_qgroup_convert_reserved_meta(inode->root,
						   qgroup_to_release);
}

static void btrfs_calculate_inode_block_rsv_size(struct btrfs_fs_info *fs_info,
						 struct btrfs_inode *inode)
{
	struct btrfs_block_rsv *block_rsv = &inode->block_rsv;
	u64 reserve_size = 0;
	u64 qgroup_rsv_size = 0;
	u64 csum_leaves;
	unsigned outstanding_extents;

	lockdep_assert_held(&inode->lock);
	outstanding_extents = inode->outstanding_extents;

	/*
	 * Insert size for the number of outstanding extents, 1 normal size for
	 * updating the inode.
	 */
	if (outstanding_extents) {
		reserve_size = btrfs_calc_insert_metadata_size(fs_info,
						outstanding_extents);
		reserve_size += btrfs_calc_metadata_size(fs_info, 1);
	}
	csum_leaves = btrfs_csum_bytes_to_leaves(fs_info,
						 inode->csum_bytes);
	reserve_size += btrfs_calc_insert_metadata_size(fs_info,
							csum_leaves);
	/*
	 * For qgroup rsv, the calculation is very simple:
	 * account one nodesize for each outstanding extent
	 *
	 * This is overestimating in most cases.
	 */
	qgroup_rsv_size = (u64)outstanding_extents * fs_info->nodesize;

	spin_lock(&block_rsv->lock);
	block_rsv->size = reserve_size;
	block_rsv->qgroup_rsv_size = qgroup_rsv_size;
	spin_unlock(&block_rsv->lock);
}

static void calc_inode_reservations(struct btrfs_fs_info *fs_info,
				    u64 num_bytes, u64 *meta_reserve,
				    u64 *qgroup_reserve)
{
	u64 nr_extents = count_max_extents(num_bytes);
	u64 csum_leaves = btrfs_csum_bytes_to_leaves(fs_info, num_bytes);
	u64 inode_update = btrfs_calc_metadata_size(fs_info, 1);

	*meta_reserve = btrfs_calc_insert_metadata_size(fs_info,
						nr_extents + csum_leaves);

	/*
	 * finish_ordered_io has to update the inode, so add the space required
	 * for an inode update.
	 */
	*meta_reserve += inode_update;
	*qgroup_reserve = nr_extents * fs_info->nodesize;
}

int btrfs_delalloc_reserve_metadata(struct btrfs_inode *inode, u64 num_bytes)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *block_rsv = &inode->block_rsv;
	u64 meta_reserve, qgroup_reserve;
	unsigned nr_extents;
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_FLUSH_ALL;
	int ret = 0;

	/*
	 * If we are a free space inode we need to not flush since we will be in
	 * the middle of a transaction commit.  We also don't need the delalloc
	 * mutex since we won't race with anybody.  We need this mostly to make
	 * lockdep shut its filthy mouth.
	 *
	 * If we have a transaction open (can happen if we call truncate_block
	 * from truncate), then we need FLUSH_LIMIT so we don't deadlock.
	 */
	if (btrfs_is_free_space_inode(inode)) {
		flush = BTRFS_RESERVE_NO_FLUSH;
	} else {
		if (current->journal_info)
			flush = BTRFS_RESERVE_FLUSH_LIMIT;

		if (btrfs_transaction_in_commit(fs_info))
			schedule_timeout(1);
	}

	num_bytes = ALIGN(num_bytes, fs_info->sectorsize);

	/*
	 * We always want to do it this way, every other way is wrong and ends
	 * in tears.  Pre-reserving the amount we are going to add will always
	 * be the right way, because otherwise if we have enough parallelism we
	 * could end up with thousands of inodes all holding little bits of
	 * reservations they were able to make previously and the only way to
	 * reclaim that space is to ENOSPC out the operations and clear
	 * everything out and try again, which is bad.  This way we just
	 * over-reserve slightly, and clean up the mess when we are done.
	 */
	calc_inode_reservations(fs_info, num_bytes, &meta_reserve,
				&qgroup_reserve);
	ret = btrfs_qgroup_reserve_meta_prealloc(root, qgroup_reserve, true);
	if (ret)
		return ret;
	ret = btrfs_reserve_metadata_bytes(root, block_rsv, meta_reserve, flush);
	if (ret) {
		btrfs_qgroup_free_meta_prealloc(root, qgroup_reserve);
		return ret;
	}

	/*
	 * Now we need to update our outstanding extents and csum bytes _first_
	 * and then add the reservation to the block_rsv.  This keeps us from
	 * racing with an ordered completion or some such that would think it
	 * needs to free the reservation we just made.
	 */
	spin_lock(&inode->lock);
	nr_extents = count_max_extents(num_bytes);
	btrfs_mod_outstanding_extents(inode, nr_extents);
	inode->csum_bytes += num_bytes;
	btrfs_calculate_inode_block_rsv_size(fs_info, inode);
	spin_unlock(&inode->lock);

	/* Now we can safely add our space to our block rsv */
	btrfs_block_rsv_add_bytes(block_rsv, meta_reserve, false);
	trace_btrfs_space_reservation(root->fs_info, "delalloc",
				      btrfs_ino(inode), meta_reserve, 1);

	spin_lock(&block_rsv->lock);
	block_rsv->qgroup_rsv_reserved += qgroup_reserve;
	spin_unlock(&block_rsv->lock);

	return 0;
}

/**
 * btrfs_delalloc_release_metadata - release a metadata reservation for an inode
 * @inode: the inode to release the reservation for.
 * @num_bytes: the number of bytes we are releasing.
 * @qgroup_free: free qgroup reservation or convert it to per-trans reservation
 *
 * This will release the metadata reservation for an inode.  This can be called
 * once we complete IO for a given set of bytes to release their metadata
 * reservations, or on error for the same reason.
 */
void btrfs_delalloc_release_metadata(struct btrfs_inode *inode, u64 num_bytes,
				     bool qgroup_free)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	num_bytes = ALIGN(num_bytes, fs_info->sectorsize);
	spin_lock(&inode->lock);
	inode->csum_bytes -= num_bytes;
	btrfs_calculate_inode_block_rsv_size(fs_info, inode);
	spin_unlock(&inode->lock);

	if (btrfs_is_testing(fs_info))
		return;

	btrfs_inode_rsv_release(inode, qgroup_free);
}

/**
 * btrfs_delalloc_release_extents - release our outstanding_extents
 * @inode: the inode to balance the reservation for.
 * @num_bytes: the number of bytes we originally reserved with
 *
 * When we reserve space we increase outstanding_extents for the extents we may
 * add.  Once we've set the range as delalloc or created our ordered extents we
 * have outstanding_extents to track the real usage, so we use this to free our
 * temporarily tracked outstanding_extents.  This _must_ be used in conjunction
 * with btrfs_delalloc_reserve_metadata.
 */
void btrfs_delalloc_release_extents(struct btrfs_inode *inode, u64 num_bytes)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	unsigned num_extents;

	spin_lock(&inode->lock);
	num_extents = count_max_extents(num_bytes);
	btrfs_mod_outstanding_extents(inode, -num_extents);
	btrfs_calculate_inode_block_rsv_size(fs_info, inode);
	spin_unlock(&inode->lock);

	if (btrfs_is_testing(fs_info))
		return;

	btrfs_inode_rsv_release(inode, true);
}

/**
 * btrfs_delalloc_reserve_space - reserve data and metadata space for
 * delalloc
 * @inode: inode we're writing to
 * @start: start range we are writing to
 * @len: how long the range we are writing to
 * @reserved: mandatory parameter, record actually reserved qgroup ranges of
 * 	      current reservation.
 *
 * This will do the following things
 *
 * - reserve space in data space info for num bytes
 *   and reserve precious corresponding qgroup space
 *   (Done in check_data_free_space)
 *
 * - reserve space for metadata space, based on the number of outstanding
 *   extents and how much csums will be needed
 *   also reserve metadata space in a per root over-reserve method.
 * - add to the inodes->delalloc_bytes
 * - add it to the fs_info's delalloc inodes list.
 *   (Above 3 all done in delalloc_reserve_metadata)
 *
 * Return 0 for success
 * Return <0 for error(-ENOSPC or -EQUOT)
 */
int btrfs_delalloc_reserve_space(struct btrfs_inode *inode,
			struct extent_changeset **reserved, u64 start, u64 len)
{
	int ret;

	ret = btrfs_check_data_free_space(inode, reserved, start, len);
	if (ret < 0)
		return ret;
	ret = btrfs_delalloc_reserve_metadata(inode, len);
	if (ret < 0)
		btrfs_free_reserved_data_space(inode, *reserved, start, len);
	return ret;
}

/**
 * btrfs_delalloc_release_space - release data and metadata space for delalloc
 * @inode: inode we're releasing space for
 * @start: start position of the space already reserved
 * @len: the len of the space already reserved
 * @release_bytes: the len of the space we consumed or didn't use
 *
 * This function will release the metadata space that was not used and will
 * decrement ->delalloc_bytes and remove it from the fs_info delalloc_inodes
 * list if there are no delalloc bytes left.
 * Also it will handle the qgroup reserved space.
 */
void btrfs_delalloc_release_space(struct btrfs_inode *inode,
				  struct extent_changeset *reserved,
				  u64 start, u64 len, bool qgroup_free)
{
	btrfs_delalloc_release_metadata(inode, len, qgroup_free);
	btrfs_free_reserved_data_space(inode, reserved, start, len);
}
