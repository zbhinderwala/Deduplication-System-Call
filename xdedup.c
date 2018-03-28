#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#ifndef __NR_xdedup
#error xdedup system call not defined
#endif

#define MAXSIZE 100

#define FLAG_N  0x01 // decimal 1
#define FLAG_P  0x02 // decimal 2
#define FLAG_D  0x04 // decimal 4

/**
 *Helper function to print usage
 */
void help(){
	printf("Usage : ./xdedup [-npd] file1.txt file2.txt [outputfile.txt]\n");
}

/**
 *Function to check if arguments passed are null or not
 */
int checkForNull(int argc,char *argv[]){
	int i=0;
	for(i=1;i<argc;i++){
		if(!strcmp(argv[i],"null") || !strcmp(argv[i],"NULL")|| argv[i]=='\0'){
			printf("Arguments passed as null\n");
			help();
			return 0;
		}

	}
	return 1;


}


/**
 *Function to check if file exists with the required permissions
 *
 */

int checkFile(char *file1){
	int retval;
	struct stat buffer;
	retval = access(file1,F_OK);

	if(errno == ENOENT){
		printf("%s does not exist.Please enter a valid file\n",file1);
		return retval; 
	}
	retval = access(file1, R_OK);
       	if (retval!=0){
               	printf("%s is not readable.Permision denied\n",file1);
               	return retval;
       	}


	if( errno == EACCES){
		printf("%s is not accessible.Please enter a valid file\n",file1);
		return retval;
	
	}
	
	retval = stat(file1,&buffer);
	if ( retval != 0){
		printf("%s file does not exist\n",file1);
		return retval;

	}
	if(!S_ISREG(buffer.st_mode)){
		printf("%s is not a file.Please enter a valid file\n",file1);
		return -1;

	}
	if( !(buffer.st_mode & S_IRUSR))
	{
		printf("%s is not readable.Permission denied\n",file1);
		return -1;
	}
	return retval;
}


/*
 *Function to check the permissions of output file provided
 */
int checkOutFile(char *file){
	int retval;
	struct stat buffer;
	retval = access(file,F_OK);
        if (retval==0){
		retval = stat(file,&buffer);
		if(!(buffer.st_mode & S_IWUSR)){
			printf("%s is not writable.Permision denied\n",file);
                        return -1; 

		}
		if(!S_ISREG(buffer.st_mode)){
			printf("%s is not a file.Please enter a valid file\n",file);
			return -1;
		}	
        }
	else
		return 0;
	
	return retval;
}

/*
 *Function to print various errors to the user
 */
void errorChecking(int ret){

	switch(errno){
		case (EPERM):
			printf("Operation not permitted error -%d\n",errno);
			break;
		case (ENOENT):
			printf("No such file error -%d\n",errno);
			break;
		case (ENOMEM):
			printf("No memory error -%d\n",errno);
			break;
		case (EACCES):
                        printf("Permission denied error -%d\n",errno);
                        break;
                case (EFAULT):
                        printf("Bad address error -%d\n",errno);
                        break;
                case (ENOSPC):
                        printf("No space left on devide error -%d\n",errno);
                        break;
		case (EISDIR):
			printf("File specified is a directory error -%d\n",errno);
			break;
		case (200):
			printf("Files owner not identical error -%d\n",errno);
			break;
		case(201):
			printf("File sizes are not same error -%d\n",errno);
			break;
		case(300):
			printf("Error while reading -%d\n",errno);
			break;
		case(301):
			printf("Files are not identical error -%d\n",errno);
			break;
		case(302):
			printf("Could not write to output file error -%d\n",errno);
			break;
		case(303):
			printf("Output file corrupted and could not delete error -%d\n",errno);
			break;
		case(304):
			printf("Output file could not be overwritten error -%d\n",errno);
			break;
		case(305):
			printf("Files could not be deduplicated error -%d\n",errno);
			break;
		default:
			printf("Syscall error -%d\n",errno);
			break;
	}


}


/*
 *
 *Main class that call the sys_xdedup syscall
 */

int main(int argc,char *argv[])
{
	struct myargs{
		char *file1;
		char *file2;
		char *file3;
		u_int option;
	};
	u_int opts=0x00;
	int rc=0;
	int opt=0;
	u_int partial = 0;
	struct myargs *args;
	if ( argc < 3){
		help();
		return 0;
	}
	else{
		if(!checkForNull(argc,argv)){
			return 0;
		}
	}
	/* Read all options from user and set bits accordingly*/
	while ((opt = getopt(argc,argv,"npd")) != -1) {
	
        	switch (opt) {
        		case 'n':
		        	opts |= FLAG_N;
				break;
        		case 'p':
            			opts |= FLAG_P;	
				break;
			case 'd':
				opts |= FLAG_D;
				break;
        		default: /* '?' */
				printf("Invalid options passed\n");
				help();
               			exit(EXIT_FAILURE);
        	}
	}
	
	/* Just verify correct arguments are passed for option n , output file is NOT passed */
	if ( argc - optind < 2){
		printf("Number of arguments passed is invalid\n");
		help();
		return 0;	
	}
	if ((opts & FLAG_N) && (argc - optind >2 ) ){
		printf("More arguments passed , for 'n' do not require output file\n");
		help();
		return 0;
	}
	
	/* Verify if only p is passed and output file is given*/ 	
	if((opts & FLAG_P)&&!((opts & (FLAG_N | FLAG_P))== (FLAG_N|FLAG_P)) ){
		if(argc-optind<3){
			printf("Output file required when 'p' is set\n");
			help();
			return 0;
		}
		else
			partial = 1;
	}
	
	
	args=(struct myargs *)(malloc(sizeof(struct myargs)));
	args->file1=(char *)(malloc(sizeof(char)*(MAXSIZE)));
	args->file2=(char *)(malloc(sizeof(char)*(MAXSIZE))); 
	strcpy(args->file1,argv[optind]);
	strcpy(args->file2,argv[optind+1]);
	if(partial){
		args->file3=(char *)(malloc(sizeof(char)*(MAXSIZE))); 
		strcpy(args->file3,argv[optind+2]);
	}
	args->option = opts;
		
	/* Checking if files passed exist and allow us to read */
	rc = checkFile(args->file1);
	if (rc!=0){
		goto cleanup;
	}
	
	rc = checkFile(args->file2);
	if(rc!=0)
		goto cleanup;
	
	if(partial==1){
		rc = checkOutFile(args->file3);
		if(rc !=  0)
			goto cleanup;

	}

	// Calling xdedup system call
	rc = syscall(__NR_xdedup,(void *)args);
	
	if (rc >= 0){
		printf("Number of bytes identical are %d\n", rc);
	}	
	else
		errorChecking(rc);	
	
		
	cleanup:
		free(args->file1);
		free(args->file2);
		if(partial == 1)
			free(args->file3);
		free(args);
	exit(rc);
}
