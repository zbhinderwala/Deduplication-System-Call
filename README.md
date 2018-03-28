# Deduplication-System-Call

The submission contains the folowing files
1. xdedup.c // Main c program that call the sys_xdedup syscall
2. sys_xdedup.c // Implementation of deduplication system call
3. Makefile
4. kernel.config // Minimal kernel configuration
5. README

The system call for deduplication can be run with the following command.

./xdedup [-npd] file1.txt file2.txt [file3.txt]

Implementation Details:

1. xdedup.c
This file has main function that reads the commands passed to the user.It stores the options passed if any.Firstly it check for required arguments passed then it goes onto to verify the flag passed and the required files or extra files passed along with the flags respectively.
After the arguments with flags are verified. the input files are verified if they are accessible and readable/writable. This has been achieved by using stat call and checking the permissions of the files. Once all the checks are passed then these arguments are packed in to a void struct and sent to the syscall. The syscall either returns number of bytes n successfull or error number.
The error number is then checked and appropriate message is displayed to the user.

2. sys_xdedup.c
This is the file which defines the deduplication system call xdedup. 
In this file we first get the arguments from the user in to a struct in kernel. Then the arguments are verified one by one.
First the 2 input files are verified for no errors.
Then the options are checked for -p where outfile file is passed.
Further check is done if output file exist or not , if not it is created else a temp file is created
Next we check if option -n was set or no options were set and file owner and sizes are different then the error is sent back to the userland.
Next we proceed to reading the 2 file buffer size of 4096 at a time and comparing it byte by byte , if any mismatch is found , comparison stops.
In case of only -n or no options error is sent back as files are not identical , incase of partial match and -p set the match is written to output file.
If files are matched completely then count is returned when -n is set.
For completedly identical files have checked their inode number and super blocks uuid to check if theu are actually the same file, in this case we cannot unlink.
If files are differnet and a perfect match them the second file is linked to first file and unlinked.

Also in case of partial writing to temp file after success , temp file is renamed to output file and output file is overwritten.

If all is successfully the call returns the number of bytes identical in the 2 files or erron number is returned.
