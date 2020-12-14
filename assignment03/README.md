# Details of assignment03
This readme file contains the answers to Questions

# Questions

## Q1
ANS: I did this lab by myslef  
## Q2: Details for me to finish this lab  

### Have a working assginment02 configuration
require a working correctly assignment02 configuration

### Modify the CUPID leaf(x4ffffffe) function to provide exit count for individual exit
see the code inside  
assignment02/linux/arch/x86/kvm/cpuid.c query_exits(), x4ffffffe leaf function  
assignment02/linux/arch/x86/kvm/vmx/vmx.c cmpe283_increase() and handle_exits() in vmx/vmx.c file

### Start nested VM(L1)
In my case, i am configuring a headless nestesd server. Make sure you press ENTER key when you see a ESC ^]
on console to enter to the nested vm console.
```
& sudo virsh start yourvmname 
& sudo virsh console yourvmname
```
### make a user-mode program to call CUPID at x4ffffffe

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
rest of the exits are 0
```
