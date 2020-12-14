# Details of assignment03
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

## Q3&4

Q3: Comment on the frequency of exits – does the number of exits increase at a stable rate? Or are there more exits performed during certain VM operations? Approximately how many exits does a full VM boot entail?  
Ans: Nope, the number of exits does not increase at a stable rate.I have a lot exits on
External Interrrupt Exit, Interrupt Window Exit, Control-register access exits, and IO Exits(attched some outputs below). In my case, total exits is 515,155 after boot up by using dmesg.  

Q4: Of the exit types defined in the SDM, which are the most frequent? Least?  
Ans: In my case, External Interrupt(1) CPUID(10), Control-register acesses(28), I/O instrcution(30), and few others are frequent. MOV DR(29), Exception(0), APIC access(44), and etc. are least. below is the output for this assgiment. 

```
cpudi(0x4FFFFFFFE), exit number =0, exits=9  
cpudi(0x4FFFFFFFE), exit number =1, exits=34,266  
cpudi(0x4FFFFFFFE), exit number =7, exits=8,709  
cpudi(0x4FFFFFFFE), exit number =10, exits=139,717  
cpudi(0x4FFFFFFFE), exit number =12, exits=50,734  
cpudi(0x4FFFFFFFE), exit number =28, exits=102,212
cpudi(0x4FFFFFFFE), exit number =29, exits=2
cpudi(0x4FFFFFFFE), exit number =30, exits=216,954
cpudi(0x4FFFFFFFE), exit number =31, exits=385
cpudi(0x4FFFFFFFE), exit number =32, exits=86,133
cpudi(0x4FFFFFFFE), exit number =44, exits=22
cpudi(0x4FFFFFFFE), exit number =48, exits=1,095
cpudi(0x4FFFFFFFE), exit number =49, exits=1,953
cpudi(0x4FFFFFFFE), exit number =54, exits=9
```
