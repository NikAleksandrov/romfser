/*	romfser	- romfs implementation that can read/substitute/extract
 *		  from romfs image file
 *	Compilation: gcc -Wall romfser.c -o romfser
 *
 *	Author: Nikolay Aleksandrov (razor@blackwall.org) 2007
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define	NEXT_MASK	0x7ffffff0
#define	CHECK_ARG(x)	if (x == NULL)	error("Argument not supplied for option\n");

#define	ROMFS_ID	"-rom1fs-"
enum {
	ROMFS_HARD,	//0		hard link
	ROMFS_DIR,	//1		directory
	ROMFS_REG,	//2		regular file
	ROMFS_LNK,	//3		symbolic link
	ROMFS_BLK,	//4		block device
	ROMFS_CHR,	//5		char device
	ROMFS_SCK,	//6		socket
	ROMFS_FIF,	//7		fifo
	ROMFS_EXEC,	//8		exec flag
};

#define	MAX_SPEC	0x7
#define	MAX_FUNCS	10
#define	MAX_NAME	45
#define	MAX_PARENT	1024

#define	PRINT_START	printf("%-45s%-15s%-15s%-15s%-10s\n", "[ name ]","[ next ]","[ size ]","[ type ]","[ exec ]");

#define	READ_BLOCK	4096
#define ALIGNUP16(x) (((x)+15)&~15)
#define	usage()		printf("Usage: %s <image file> <offset in image> <flags> [flags args]\n\n"	\
			"Example: %s a.img 802 le touch.css\n"						\
			"The previous example lists all files and directories of the RomFS\n"		\
			"image a.img with RomFS starting at 802 bytes offset from the beginning\n"	\
			"of the image and extracts touch.css in the current directory\n\n"		\
			"Flags: l - list, e - extract 1 file (1 arg), a - extract all with dir\n"	\
			"	structure in current directory, s - substitute  two files the new\n"	\
			"	file should be <= the other filesize since it's overwritten without\n"	\
			"	any header changes\n", argv[0], argv[0])

struct	romfs_file
{
	unsigned	next;
	unsigned	spec;
	unsigned	size;
	unsigned	check;
	char		name[0];
};

char	*specs[] =
{
	"Hard link",
	"Directory",
	"Regular file",
	"Symbolic link",
	"Block device",
	"Char device",
	"Socket",
	"FIFO",
	NULL
};

struct	func_desc
{
	int	(*func)();
	char	name[MAX_NAME];
	char	sname[MAX_NAME];
	char	parentstr[MAX_PARENT];
};

struct func_desc	exec_funcs[MAX_FUNCS];

/*		function protos		*/
void		print_inode(char*, unsigned, unsigned);
void		error(char*);
int		report(struct func_desc *, struct romfs_file *);
int		extract_inode(struct func_desc *, struct romfs_file *);	/*	extracts dirs/files	*/
int		sub_inode(struct func_desc *, struct romfs_file*);	/*	substitute file	inside */
int		extract_image(struct func_desc *, struct romfs_file *); /*	extracts everything */
unsigned	count_slashes(char*);
size_t		my_strlcpy(char *, char *, size_t);

/*		global variables	*/
char		*addrptr;	/*	mmap addr holder	*/
unsigned	next;		/*	next file offset	*/
unsigned	flags=0;		/*	list flag	*/

int	main(argc, argv)
		int	argc;
		char	**argv;
{
	int		i,off,fd,flagnum;
	char		flags[64];
	struct	stat	getstat;

	if (argc < 4)
	{
		usage();
		return	-1;
	}

	fd = open(argv[1], O_RDWR);

	if (stat(argv[1], &getstat) == -1)
	{
		perror("stat");
		return	-1;
	}

	argc--;argv++;
	off = atoi(argv[1]);
	argc--;argv++;

	/*	flags	*/
	my_strlcpy(flags,argv[1],64);
	flagnum = strlen(flags)%MAX_FUNCS;
	argc--;argv++;
	for(i=0;i<flagnum;i++)
	{
		switch(flags[i])
		{
			case	'e':
				CHECK_ARG(argv[1]);
				exec_funcs[i].func = extract_inode;
				my_strlcpy(exec_funcs[i].name, argv[1], MAX_NAME);
				argc--;argv++;
			break;

			case	'l':
				exec_funcs[i].func = report;
			break;

			case	's':
				CHECK_ARG(argv[1]);
				CHECK_ARG(argv[2]);
				exec_funcs[i].func = sub_inode;
				my_strlcpy(exec_funcs[i].name, argv[1], MAX_NAME);
				my_strlcpy(exec_funcs[i].sname, argv[2], MAX_NAME);
				argc-=2;argv+=2;
			break;

			case	'a':
				exec_funcs[i].func = extract_image;
			break;

			default:
				error("Unknown flag used\n");
		}
	}

	exec_funcs[i].func = NULL;

	addrptr = (char*)mmap(NULL, getstat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, off);

	if (addrptr == MAP_FAILED)
	{
		perror("mmap");
		return	-1;
	}

	if (strcmp(addrptr,ROMFS_ID))
	{
		error("Not recognized as ROMFS\n");
	}

	next = ALIGNUP16(16+(strlen((addrptr+16))+1));	/*	first node	*/

	PRINT_START;
	print_inode("/", next, next);

	return	0;
}

void	print_inode(char *parent, unsigned off, unsigned offparent)
{
	unsigned		oldoff=0,i,cnt;
	struct	romfs_file	*tempfile;
	char			customparent[MAX_PARENT];

	for(cnt=0;;cnt++)
	{
		if (oldoff == off)
		{
			error("possible borken fs endless loop\n");
		}

		tempfile = (struct romfs_file*)((char*)addrptr+off);

		for(i=0;exec_funcs[i].func!=NULL;i++)
		{
			snprintf(exec_funcs[i].parentstr, MAX_PARENT, "%s",parent);
			exec_funcs[i].func(&exec_funcs[i], tempfile);
		}

		if (tempfile->name[0] != '.' && (htonl(tempfile->next)&MAX_SPEC) == ROMFS_DIR)
		{
			snprintf(customparent, MAX_PARENT, "%s%s/",parent,tempfile->name);
			print_inode(customparent, off+32, off);
		}

		oldoff = off;
		off = (htonl(tempfile->next)&NEXT_MASK);

		if (off == 0)
		{
			return;
		}
	}
}

int	report(struct func_desc *func, struct romfs_file *file)
{
	char	tempname[MAX_PARENT];

	if (file->name[0] != '.')
		snprintf(tempname, sizeof(tempname), "%s%s",func->parentstr,file->name);
	else
		my_strlcpy(tempname, file->name, MAX_PARENT);

	printf("%-45s", tempname);
	printf("%-15u", htonl(file->next)&NEXT_MASK);
	printf("%-15u", htonl(file->size));
	printf("%-15s", specs[(htonl(file->next)&MAX_SPEC)]);
	printf("%-10s\n", (htonl(file->next)&ROMFS_EXEC) ? "Yes" : "No");

	return 0;
}

int	extract_inode(struct func_desc *func, struct romfs_file *file)
{
	int	filefd=0,size=0;
	char	*writeptr;
	char		fpath[MAX_PARENT];

	if (strcmp(file->name, func->name))
		return	-1;

	snprintf(fpath, MAX_PARENT, "./%s%s",func->parentstr, file->name);
	if ((htonl(file->next)&MAX_SPEC) == ROMFS_DIR)
	{
		if (mkdir(fpath, 0755) == -1)
		{
			perror("mkdir");
			return	-1;
		}
	}
	else
	{
		if ((filefd = open(fpath, O_CREAT|O_WRONLY)) == -1)
		{
			if ((filefd = open(file->name, O_CREAT|O_WRONLY)) == -1)
			{
				perror("open");
				return	-1;
			}
		}

		writeptr = (char*)((char*)file + sizeof(struct romfs_file)+ALIGNUP16(strlen(file->name))+1);
		size = htonl(file->size);

		if (write(filefd, writeptr, size) == -1)
		{
			perror("write");
			return	-1;
		}

		close(filefd);
	}

	return	0;
}

int	sub_inode(struct func_desc *func, struct romfs_file *file)
{
	int	filefd,i,readsiz=0;
	char	*writeptr;
	char	readstring[READ_BLOCK];	/*	default reading/writing 4k blocks	*/

	if (strcmp(func->name, file->name))
		return	0;

	if ((filefd = open(func->sname, O_RDONLY)) == -1)
	{
		perror("open");
		return	-1;
	}

	writeptr = (char*)((char*)file + sizeof(struct romfs_file)+ALIGNUP16(strlen(file->name))+1);
	readsiz = htonl(file->size)<READ_BLOCK ? htonl(file->size) : READ_BLOCK;

	for(i=0;i<htonl(file->size);i+=READ_BLOCK)
	{
		read(filefd, readstring, readsiz);
		memcpy(writeptr, readstring, readsiz);
		writeptr+=readsiz;
	}

	if (msync(file, htonl(file->size)+sizeof(struct romfs_file)+ALIGNUP16(strlen(file->name))+1, MS_SYNC) == -1)
	{
		perror("msync");
		return	-1;
	}

	return	0;
}

int	extract_image(struct func_desc *func, struct romfs_file *file)
{
	if (strlen(file->name) <= 2 && file->name[0] == '.')
		return	0;

	my_strlcpy(func->name, file->name, MAX_NAME);

	return	extract_inode(func, file);
}


void	error(char *err)
{
	printf("Exit with error: %s\n", err);
	exit (-1);
}

unsigned	count_slashes(char *str)
{
	unsigned	i,slashes;

	for(i=0,slashes=0;str[i]!=0;i++)
		if (str[i] == '\\')
			slashes++;

	return	slashes;
}

size_t my_strlcpy(char *d, char *s, size_t len)
{
	size_t cnt;

	if (!len)
		return 0;

	for (cnt = 0; cnt < len-1 && cnt < strlen(s); cnt++)
		d[cnt] = s[cnt];

	d[cnt] = '\0';

	return cnt;
}

