# Details of assignment02
This readme file contains the answers to Questions

# Questions

## Q1
ANS: I did this lab by myslef  
## Q2Details for me to finish this lab  

### Lunch a GCP instance(L0-VM) that has VMX feature enable and download packages for hosting nested vm

download packges for hosting nested vm   
```
$ sudo apt-get install qemu-kvm libvirt-bin virtinst bridge-utils cpu-checker
```
verify vmx feature&kvm installation
```
grep -wc vmx/svm /proc/cpuinfo
```


### Git clone the linux source code and compile it  

```
$ git clone https://github.com/ruiyang00/linux.git
$ cd linux
$ sudo bash
$ apt-get install build-essential kernel-package fakeroot libncurses5-dev libssl-dev ccache bison flex libelf-dev 
$ uname -a
$ cp /boot/config-your_version_of_linux    ./.config
$ make oldconfig
$ make && make modules && make modules_install && make install
$ reboot
```
### Create a nested VM(L1-VM) on top of L0
Download a linux distro as you like for my I choose centos07
```
$ wget "http://mirrors.ocf.berkeley.edu/centos/7.8.2003/isos/x86_64/CentOS-7-x86_64-DVD-2003.iso"
$ virt-install  --network bridge:virbr0 --name yourvmname \
 --os-variant=centos7.0 --ram=1024 --vcpus=1  \
 --disk path=/var/lib/libvirt/images/yourvmname-os.qcow2,format=qcow2,bus=virtio,size=5 \
  --graphics none  --location=/your/path/to/image/CentOS-7-x86_64-DVD-1511.iso \
  --extra-args="console=tty0 console=ttyS0,115200"  --check all=off
```   
### Modify the CUPID leaf function to add a total exits&cycles info
see the code inside assignment02/linux/arch/x86/kvm/cpuid.c and assignment02/linux/arch/x86/kvm/vmx/vmx.c

### Start nested VM(L1)
In my case, i am configuring a headless nestesd server. Make sure you press ENTER key when you see a ESC ^]
on console to enter to the nested vm console.
```
& sudo virsh start yourvmname 
& sudo virsh console yourvmname
```
### make a user-mode program to call CUPID at x4fffffff

##Q3
Does the number of exits increase at a stable rate?
No, the exits has a gap between my cpuid calls. I think that before I make call to my test program, the kernel has other exits. For example, I/O to disk or Page Fault Exits.

Approximately how many exits does a full vm boot entail?
In my case

   



