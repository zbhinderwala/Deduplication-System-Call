#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <asm/segment.h>
#include <linux/namei.h>

asmlinkage extern long (*sysptr)(void *arg);

#define MAXSIZE 100
 
#define FLAG_N  0x01 // decimal 1
#define FLAG_P  0x02 // decimal 2
#define FLAG_D  0x04 // decimal 4

#define BUFFERSIZE 4096
/*
*Function to compare the data in the buffer and return matching count
*/
int compare(char *buff1,char *buff2,int buff1_length, int buff2_length){
        int i,size;
        int count = 0;
        if( buff1_length <buff2_length)
                size = buff1_length;
        else
                size = buff2_length;
        for( i=0;i<size;i++){
                if(buff1[i] != buff2[i])
                        break;
                count++;
        }
	
        return count;
}

/*
*Function to rename a file
*/
int rename(char *oldfile,char *newfile){
	int ret = 0;
	struct path oldpath;
	struct path newpath;
	
	kern_path(oldfile, LOOKUP_FOLLOW,&oldpath);
	kern_path(newfile, LOOKUP_FOLLOW, &newpath);
	ret= vfs_rename(newpath.dentry->d_parent->d_inode,newpath.dentry
		,oldpath.dentry->d_parent->d_inode,oldpath.dentry,NULL,0);
	return ret;


}

/*
*Function to dedup a file if found identical
*
*/
int dedup(char *oldfile,char *newfile)
{
	struct path oldpath;
	struct path newpath;
	int retvfs = 0,retun=0;
	struct inode *old_parent,*old;
	struct inode *new_parent,*new;
	kern_path(oldfile, LOOKUP_FOLLOW, &oldpath);
	kern_path(newfile, LOOKUP_FOLLOW, &newpath);
	old_parent = oldpath.dentry->d_parent->d_inode;
	new_parent = newpath.dentry->d_parent->d_inode;
	old= oldpath.dentry->d_inode;
	new= newpath.dentry->d_inode;
	/*Check if both files are same and on the same filesystem then do
  			 not delete it */
	if( old->i_ino == new->i_ino){
		// Checkc  if the files are in the same super block
		if(strcmp(old->i_sb->s_uuid,new->i_sb->s_uuid)==0){
			printk("XDEDUP ERROR:Both the files are the same hence cananot deduplicate");
			return -1;
		}	
	
	}
	mutex_lock(&new_parent->i_mutex);
	retvfs = vfs_link(oldpath.dentry,newpath.dentry->d_parent->d_inode
			,newpath.dentry,NULL);
	printk("XDEDUP INFO:File %s linked to file %s successfully\n",oldfile,newfile);
	mutex_unlock(&new_parent->i_mutex);
	mutex_lock(&old_parent->i_mutex);
	retun = vfs_unlink(oldpath.dentry->d_parent->d_inode,oldpath.dentry,NULL);
	mutex_unlock(&old_parent->i_mutex);
	dput(oldpath.dentry);
	printk("XDEUP INFO:File %s unlinked \n",oldfile);
	return 0;
}

/*
* Function to check if argument passed is a regular file only
*/

int checkFile(struct file *fp)
{
	if(!(fp->f_inode->i_mode & S_IRUSR))
		return 0;
	return S_ISREG(fp->f_inode->i_mode);

}

/*
 *Function to check if output file is writable or not
 */
int checkOutFile(struct file *fp)
{
	if(!(fp->f_inode->i_mode & S_IWUSR))
		return 0;
	return 1;
}

/*
 *Function to delete output or temporary file incase write fails
 *
 */
int delete_Output(char *file){

	struct path oldpath;
        int ret=0;
        struct inode *old_parent;
        kern_path(file, LOOKUP_FOLLOW, &oldpath);
        old_parent = oldpath.dentry->d_parent->d_inode;
	mutex_lock(&old_parent->i_mutex);
	ret = vfs_unlink(oldpath.dentry->d_parent->d_inode,oldpath.dentry,NULL);
	mutex_unlock(&old_parent->i_mutex);
	if(ret<0){
		printk("XDEDUP ERROR :Could not delete corrupted file %s error %d\n"
			,file,ret);
		return -1;
	}
	return ret;
}

asmlinkage long xdedup(void *arg)
{	

	/*dummy syscall: returns 0 for non null, -EINVAL for NULL */
        struct myargs{
		char *file1;
		char *file2;
		char *file3;
		u_int option;
	};
	
    	char *kbuf1;
	char *kbuf2;
    	struct file *fp1;
	struct file *fp2;
	struct file *fp3=NULL;
	struct file *fp4=NULL;
	unsigned int write=0,only_D = 0;
	struct myargs *args;
	mm_segment_t oldfs;
	unsigned short mode;
    	unsigned long long offset1 = 0 ;
	unsigned long long offset2 = 0;
	unsigned long long offset3 = 0;
    	int ret,ret2,ret3;
	int count = 0,match = 0;
	unsigned int check_Identical=0,duplicate = 0;
	char *tmp_filename;
	
	args  = (struct myargs *)(kmalloc(sizeof(struct myargs),GFP_KERNEL));
	if(copy_from_user(args,(struct myargs *)arg,sizeof(struct myargs)) != 0)
		return -EFAULT;
	if(args->option & FLAG_D){
		printk("XDEDUP DEBUG:In debug mode\n");
		printk("XDEDUP DEBUG:First file is %s\n",args->file1);
		printk("XDEDUP DEBUG:Second file is %s\n",args->file2);
	}	
	if( args->option && (args->option & FLAG_P) && !((args->option 
				& (FLAG_N|FLAG_P)) == (FLAG_N|FLAG_P) )  ){
		write = 1;
		if(args->option & FLAG_D)
			printk("XDEDUP DEBUG:Option -p is set. Will have to write into output file \n");
	}
	

	             
	fp1 = filp_open(args->file1, O_RDONLY, 0);
    	if (!fp1 || IS_ERR(fp1)) {
        	printk("XDEDUP ERROR:Reading file %s error %d\n",args->file1,
				 (int) PTR_ERR(fp1));
		count = (int) PTR_ERR(fp1);
        	goto file1out;  
    	}
	if(!checkFile(fp1)){
		printk("XDEDUP ERROR:File %s is not readable or not a regular file\n",args->file1);
		count = -13;
		goto file2out;
	}
	
       	fp2 = filp_open(args->file2,O_RDONLY, 0);
        if (!fp2 || IS_ERR(fp2)){
                printk("XDEDUP ERROR:Reading file %s error %d\n",
				args->file2, (int) PTR_ERR(fp2));
                count = (int) PTR_ERR(fp2);
                goto file2out;
        }
        if(!checkFile(fp2)){
                printk("File %s is not readable or not a regular file\n",args->file2);
                count = -13;
                goto file3out;

        } 
	
	if(args->option & FLAG_D)
		printk("XDEDUP DEBUG:Both the input files were successfully opened to be read\n");		

	//Check if no options passed or only option n set
	//or n and d are set that is p is not set at all and files dont match in size and owner
	if ( !args->option || !(args->option & FLAG_P)) {
		check_Identical = 1;
        	if(fp1->f_inode->i_uid.val != fp2->f_inode->i_uid.val){
			printk("XDEDUP ERROR:File owners are different ,thus cannot be identical\n");
			count=-200;	
			goto file3out;

		}
		if ( fp1->f_inode->i_size != fp2->f_inode->i_size){
                        printk("XDEDUP ERROR:File sizes are different, thus cannot dedup\n");
			count =-201;
                        goto file3out;
                }
        }

		
	if(write==1){ 
		fp3 = filp_open(args->file3,O_RDWR,0);
		if (!fp3 || IS_ERR(fp3)){
			if ((int)PTR_ERR(fp3) == -2){
                		if(args->option & FLAG_D)
					printk("XDEDUP DEBUG:Output file does not exist does creating one\n");
                		mode = fp1->f_inode->i_mode<
		fp2->f_inode->i_mode?fp1->f_inode->i_mode:fp2->f_inode->i_mode;
				fp3 = filp_open(args->file3,O_RDWR|O_CREAT,
						mode);
                		if(!fp3 || IS_ERR(fp3)){
                			printk("XDEDUP ERROR:Error creating new file %s error %d\n",args->file3,(int)PTR_ERR(fp3));
                    			count = (int) PTR_ERR(fp3);
					goto file3out; 
                       		}	
			}
			else{
				printk("XDEDUP ERROR:Writing to file %s error %d\n",args->file3, (int) PTR_ERR(fp3));
				count = (int) PTR_ERR(fp3);
				goto file3out;

			}
    		}		
		else{
			if(!checkOutFile(fp3)){
				printk("XDEDUP:ERROR File %s is not writable file\n",args->file3);
				count = -13;
				goto file3out;
			}
			if(fp3)
        		{		
				
				duplicate = 1;
				tmp_filename=(char *)
					(kmalloc(sizeof(char)*100,GFP_KERNEL));
				snprintf(tmp_filename,100,"tmp.%s.%s",current->comm,args->file3);
				if(args->option & FLAG_D)
					printk("XDEDUP DEBUG:Output File already exist,creating temp file with name %s\n",tmp_filename);
				
				fp4 = filp_open(tmp_filename,O_RDWR|O_CREAT,fp3->f_inode->i_mode);	
        			if(!fp4 || IS_ERR(fp4)){
					printk("XDEDUP ERROR:Error creating temporary file %s\n",tmp_filename);
					count = (int) PTR_ERR(fp4);
					goto file3out;
				}	
			}	
		}
		
		if(args->option & FLAG_D){
			printk("Output file is oppened for writing\n");

		}
	}	    

		
	// Assigning buffer to read	
	kbuf1 = kmalloc(BUFFERSIZE,GFP_KERNEL);
    	kbuf2 = kmalloc(BUFFERSIZE,GFP_KERNEL);
	
	oldfs=get_fs();
    	set_fs(get_ds());
    
		
	ret = vfs_read(fp1, kbuf1 , BUFFERSIZE, &offset1);
	ret2 = vfs_read(fp2, kbuf2, BUFFERSIZE, &offset2);
	
	if (ret < 0 || ret2<0){
		printk("XDEDUP ERROR:Error while reading file\n");
		count = -300;
		goto cleanup;
	}
	
	 
	while(ret>0 && ret2>0){
		//Read equal bytes 
					
		match = compare(kbuf1,kbuf2,ret,ret2);
		count += match;

		if(check_Identical == 1){
			if( count < (offset1-1) || count<(offset2-1) ){
				if(args->option & FLAG_D)
                        		printk("XDEDUP DEBUG:Files are not identical\n");
                        	count = -301;
                        	goto cleanup;
                	}
		}

					
		if( write==1 ){
		 	if(duplicate == 1)
				ret3 = vfs_write(fp4,kbuf1,match,&offset3);
			else
				ret3 = vfs_write(fp3,kbuf1,match,&offset3);
			if(ret3 < 0){
				printk("XDEDUP ERROR:Error while writing to file %d\n",ret);
				if(duplicate == 1){
					filp_close(fp4,NULL);
					filp_close(fp3,NULL);
					if(delete_Output(tmp_filename)<0){
						count = -303;
						goto cleanup;
					}	
				}
				else{
					filp_close(fp3,NULL);
					if(delete_Output(args->file3)<0){
						count = -303;
						goto cleanup;
					}
				}
				count =-302;
				goto cleanup;

			}
		}
		
					
		
		ret = vfs_read(fp1, kbuf1,BUFFERSIZE, &offset1);
		ret2 = vfs_read(fp2,kbuf2, BUFFERSIZE, &offset2);
		if (ret < 0 || ret2<0){
                	printk("XDEDUP ERROR:Error while reading file\n");
                	count = -300;
                	goto cleanup;
        	}
	
	}
	//Checking the number of bytes that are same
	//Check if only d is set
	only_D = (args->option & FLAG_D) == FLAG_D && !((args->option & (FLAG_D|FLAG_N))==(FLAG_D|FLAG_N)) && 
			!((args->option & (FLAG_D|FLAG_P)) == (FLAG_D|FLAG_P)) &&
			!((args->option & (FLAG_D|FLAG_N|FLAG_P)) == (FLAG_D|FLAG_N|FLAG_P)) ;  
	printk("flags set are %u\n",only_D);
	if( ( !args->option || only_D ) && count == offset1){
		if(args->option & FLAG_D)
			printk("XDEDUP DEBUG:Files are completely identical and will be deduplicated with bytes %d\n",count);
		filp_close(fp2,NULL);	
		if(dedup(args->file2,args->file1)<0){
			printk("XDEDUP ERROR: File cannot be deduplicated\n");
			count = -305;
			goto cleanup;
		
		}					
		if(args->option & FLAG_D)
			printk("XDEUP DEBUG:Deduplication done successfully\n");
	}
	

	/* Check if partial is set on and output file already exist , rename the temp file to output*/
	if(  write == 1 ){
		filp_close(fp3,NULL);
		if(duplicate ==1){
			filp_close(fp4,NULL);
			kfree(tmp_filename);
			if(rename(args->file3,tmp_filename)<0){
				printk("XDEDUP ERROR:File could not be overwritten\n");
				count =-304;
				goto cleanup;
			}
		}
	
	}
	
	cleanup:
		set_fs(oldfs);
		kfree(kbuf1);
		kfree(kbuf2);
	file3out:	
		filp_close(fp2,NULL);
    	file2out:    	
		filp_close(fp1,NULL);
	file1out:
		kfree(args);
	
        return count;
}	


static int __init init_sys_xdedup(void)
{
	printk("installed new sys_xdedup module\n");
	if (sysptr == NULL)
		sysptr = xdedup;
	return 0;
}
static void __exit exit_sys_xdedup(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	printk("removed sys_xdedup module\n");
}
module_init(init_sys_xdedup);
module_exit(exit_sys_xdedup);
MODULE_LICENSE("GPL");
                                     
