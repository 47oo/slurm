/* 
 * Mach_Stat_Mgr.c - Manage the node specification information of SLURM
 * See Mach_Stat_Mgr.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "list.h"
#include "Mach_Stat_Mgr.h"

#define DEBUG_MODULE 1
#define SEPCHARS " \n\t"

struct Node_Record  Default_Record;	/* Default values for node record */
struct Node_Record  Node_Record_Read;	/* Node record being read */
List   Node_Record_List = NULL;		/* List of Node_Records */

int	Delete_Record(char *name);
struct Node_Record  *Duplicate_Record(char *name);
int 	Parse_Node_Spec(char *Specification, char *My_Name, char *My_OS, 
	int *My_CPUs, int *Set_CPUs, float *My_Speed, int *Set_Speed,
	int *My_RealMemory, int *Set_RealMemory, int *My_VirtualMemory, int *Set_VirtualMemory, 
	long *My_TmpDisk, int *Set_TmpDisk, unsigned int *My_Pool, int *Set_Pool, 
	enum Node_State *NodeState, int *Set_State, time_t *My_LastResponse, int *Set_LastResponse);
void	Pool_String_To_Value (char *pool, unsigned int *Pool_Value, int *Error_Code);
void 	Pool_Value_To_String(unsigned int pool, char *Pool_String, int Pool_String_size, char *node_name);

#ifdef DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    char Out_Line[BUF_SIZE];

    if (argc < 4) {
	printf("Usage: %s <in_file> <out_file1> <out_file2>\n", argv[0]);
	exit(0);
    } /* if */
    Error_Code = Read_Node_Spec_Conf(argv[1]);
    if (Error_Code != 0) {
	printf("Error %d from Read_Node_Spec_Conf", Error_Code);
	exit(1);
    } /* if */

    Error_Code = Update_Node_Spec_Conf("Name=mx01.llnl.gov CPUs=3 TmpDisk=12345");
    if (Error_Code != 0) printf("Error %d from Update_Node_Spec_Conf\n", Error_Code);
    Error_Code = Update_Node_Spec_Conf("Name=mx03.llnl.gov CPUs=4 TmpDisk=16384 Pool=9 State=IDLE LastResponse=123");
    if (Error_Code != 0) printf("Error %d from Update_Node_Spec_Conf\n", Error_Code);

    Error_Code = Write_Node_Spec_Conf(argv[2], 1);
    if (Error_Code != 0) printf("Error %d from Write_Node_Spec_Conf", Error_Code);

    Error_Code = Dump_Node_Records(argv[3]);
    if (Error_Code != 0) printf("Error %d from Dump_Node_Records", Error_Code);

    Error_Code = Validate_Node_Spec("Name=mx03.llnl.gov CPUs=4 TmpDisk=22222");
    if (Error_Code != 0) printf("Error %d from Validate_Node_Spec", Error_Code);
    Error_Code = Validate_Node_Spec("Name=mx03.llnl.gov CPUs=3");
    if (Error_Code == 0) printf("Error %d from Validate_Node_Spec", Error_Code);

    Error_Code = Show_Node_Record("mx03.llnl.gov", Out_Line);
    if (Error_Code != 0) printf("Error %d from Show_Node_Record", Error_Code);
    if (Error_Code == 0) printf("Show_Node_Record: %s\n", Out_Line);
    exit(0);
} /* main */
#endif

/* 
 * Delete_Record - Find a record for node with specified name and delete it
 * Input: name - name of the node
 * Output: returns 0 on no error, otherwise errno
 */
int Delete_Record(char *name) {
    int Error_Code;
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Delete_Record: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Delete_Record: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    }

    Error_Code = ENOENT;
    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	if (strcmp(Node_Record_Point->Name, name) == 0) {
	    (void) list_remove(Node_Record_Iterator);
	    free(Node_Record_Point);
	    Error_Code = 0;
	    break;
	} /* if */
    } /* while */

    list_iterator_destroy(Node_Record_Iterator);
    return Error_Code;
} /* Delete_Record */


/*
 * Dump_Node_Records - Raw dump of node specification information into the specified file 
 * Input: File_Name - Name of the file into which the node specification is to be written
 * Output: return - 0 if no error, otherwise an error code
 */
int Dump_Node_Records (char *File_Name) {
    FILE *Node_Spec_File;	/* Pointer to output data file */
    int Error_Code;		/* Error returns from system functions */
    int i;			/* Counter */
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    /* Initialization */
    Error_Code = 0;
    Node_Record_Iterator = list_iterator_create(Node_Record_List);
   if (Node_Record_Iterator == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Dump_Node_Records: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Dump_Node_Records: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    }
    Node_Spec_File = fopen(File_Name, "w");
    if (Node_Spec_File == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Dump_Node_Records error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Dump_Node_Records error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */

    i = NODE_STRUCT_VERSION;
    if (fwrite((void *)&i, sizeof(i), 1, Node_Spec_File) < 1) {
	Error_Code = ferror(Node_Spec_File);
#ifdef DEBUG_MODULE
	fprintf(stderr, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#else
	syslog(LOG_ERR, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#endif
    } /* if */

    /* Process the data file */
    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	if (fwrite((void *)Node_Record_Point, sizeof (struct Node_Record), 1, Node_Spec_File) < 1) {
	    if (Error_Code == 0) Error_Code = ferror(Node_Spec_File);
#ifdef DEBUG_MODULE
	    fprintf(stderr, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#else
	    syslog(LOG_ERR, "Dump_Node_Records error %d writing to file %s\n", Error_Code, File_Name);
#endif
	} /* if */
    } /* while */

    /* Termination */
    if (fclose(Node_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#ifdef DEBUG_MODULE
	fprintf(stderr, "Dump_Node_Records error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Dump_Node_Records error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    list_iterator_destroy(Node_Record_Iterator);
    return Error_Code;
} /* Dump_Node_Records */


/* 
 * Duplicate_Record - Find a record for node with specified name, return pointer or NULL if not found
 */
struct Node_Record *Duplicate_Record(char *name) {
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    Node_Record_Iterator = list_iterator_create(Node_Record_List);
    if (Node_Record_Iterator == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Duplicate_Record:list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Duplicate_Record:list_iterator_create unable to allocate memory\n");
#endif
	return NULL;
    }

    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	if (strcmp(Node_Record_Point->Name, name) == 0) break;
    } /* while */

    list_iterator_destroy(Node_Record_Iterator);
    return Node_Record_Point;
} /* Duplicate_Record */


/* 
 * Parse_Node_Spec - Parse the node input specification, return values and set flags
 * Output: 0 if no error, error code otherwise
 */
int Parse_Node_Spec(char *Specification, char *My_Name, char *My_OS, 
	int *My_CPUs, int *Set_CPUs, float *My_Speed, int *Set_Speed,
	int *My_RealMemory, int *Set_RealMemory, int *My_VirtualMemory, int *Set_VirtualMemory, 
	long *My_TmpDisk, int *Set_TmpDisk, unsigned int *My_Pool, int *Set_Pool, 
	enum Node_State *My_NodeState, int *Set_State, time_t *My_LastResponse, int *Set_LastResponse) {
    char Scratch[BUF_SIZE];
    char *str_ptr1, *str_ptr2;
    int Error_Code, i;

    Error_Code         = 0;
    My_Name[0]         = (char)NULL;
    My_OS[0]           = (char)NULL;
    *Set_CPUs          = 0;
    *Set_Speed         = 0;
    *Set_RealMemory    = 0;
    *Set_VirtualMemory = 0;
    *Set_TmpDisk       = 0;
    *Set_Pool          = 0;
    *Set_State         = 0;
    *Set_LastResponse  = 0;

    if (Specification[0] == '#') return 0;
    if (strlen(Specification) >= BUF_SIZE) return E2BIG;
    str_ptr1 = (char *)strstr(Specification, "Name=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	strcpy(My_Name, str_ptr2);
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "OS=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+3);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	strcpy(My_OS, str_ptr2);
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "CPUs=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	*My_CPUs = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_CPUs = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "Speed=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+6);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	*My_Speed = (float) strtod(str_ptr2, (char **)NULL);
	*Set_Speed = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "RealMemory=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+11);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	*My_RealMemory = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_RealMemory = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "VirtualMemory=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+14);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	*My_VirtualMemory = (int) strtol(str_ptr2, (char **)NULL, 10);
	*Set_VirtualMemory = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "TmpDisk=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+8);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	*My_TmpDisk = strtol(str_ptr2, (char **)NULL, 10);
	*Set_TmpDisk = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "Pool=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+5);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	Pool_String_To_Value(str_ptr2, My_Pool, &Error_Code);
	*Set_Pool = 1;
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "State=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+6);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	for (i=0; i<= STATE_END; i++) {
	    if (strcmp(Node_State_String[i], "END") == 0) break;
	    if (strcmp(Node_State_String[i], Scratch) == 0) {
		*My_NodeState = i;
		*Set_State = 1;
		break;
	    } /* if */
	} /* for */
    } /* if */

    str_ptr1 = (char *)strstr(Specification, "LastResponse=");
    if (str_ptr1 != NULL) {
	strcpy(Scratch, str_ptr1+13);
	str_ptr2 = (char *)strtok(Scratch, SEPCHARS);
	*My_LastResponse = (time_t) strtol(str_ptr2, (char **)NULL, 10);
	*Set_LastResponse = 1;
    } /* if */

    return Error_Code;
} /* Parse_Node_Spec */


/* 
 * Pool_String_To_Value - Convert a pool list string to the equivalent bit mask
 * Input: pool - the pool list, comma separated numbers in the range of 0 to MAX_POOLS-1
 *        Pool_Value - Pointer to pool bit mask
 *        Error_Code - place in which to place any error code
 */
void Pool_String_To_Value (char *pool, unsigned int *Pool_Value, int *Error_Code) {
    int i, Pool_Num;
    char *Pool_Ptr;
    char *Sep_Ptr;

    *Error_Code = 0;
    *Pool_Value = 0;
    if (pool == NULL) return;
    if (pool[0] == (char)NULL) return;
    Pool_Ptr = pool;
    for (i=0; i<=MAX_POOLS; i++) {
	Pool_Num = (int)strtol(Pool_Ptr, &Sep_Ptr, 10);
	if ((Pool_Num < 0) || (Pool_Num > MAX_POOLS)) {
	    *Error_Code = EINVAL;
	    break;
	} else {
	    *Pool_Value |= (1 << Pool_Num);
	    if ((Sep_Ptr[0] == (char)NULL) || (Sep_Ptr[0] == '\n')) break;
	    Pool_Ptr = Sep_Ptr + 1;
	} /* else */
    } /* for */
} /* Pool_String_To_Value */


/* 
 * Pool_Value_To_String - Convert a pool list string to the equivalent bit mask
 * Input: pool - the pool bit mask
 *        Pool_String - the pool list, comma separated numbers in the range of 0 to MAX_POOLS-1
 *        Pool_String_size - size of Pool_String in bytes, prints warning rather than overflow
 *	  node_name - name of the node, used for error messages
 */
void Pool_Value_To_String(unsigned int pool, char *Pool_String, int Pool_String_size, char *node_name) {
    int i;
    int Max_Pools; 		/* Maximum pool number we are prepared to process */
    char Tmp_String[7];

    Pool_String[0] = (char)NULL;
    Max_Pools = MAX_POOLS;
    if (Max_Pools > 999999) {
#ifdef DEBUG_MODULE
    	fprintf(stderr, "Pool_Value_To_String error MAX_POOLS configured over too large at %d\n", Max_Pools);
#else
    	syslog(LOG_ERR, "Pool_Value_To_String error MAX_POOLS configured over too large at %d\n", Max_Pools);
#endif
	Max_Pools = 999999;
    } /* if */

    for (i=0; i<Max_Pools; i++) {
	if ((pool & (1 << i)) == 0) continue;
	sprintf(Tmp_String, "%d", i);
	if ((strlen(Pool_String)+strlen(Tmp_String)+1) >= Pool_String_size) {
#ifdef DEBUG_MODULE
    	    fprintf(stderr, "Pool_Value_To_String Pool string overflow for node Name %s\n", node_name);
#else
    	    syslog(LOG_ERR, "Pool_Value_To_String Pool string overflow for node Name %s\n", node_name);
#endif
	} /* if */
	if (Pool_String[0] != (char)NULL) strcat(Pool_String, ",");
	strcat(Pool_String, Tmp_String);
    } /* for */
} /* Pool_Value_To_String */

/*
 * Read_Node_Spec_Conf - Load the node specification information from the specified file 
 * Input: File_Name - Name of the file containing node specification
 * Output: return - 0 if no error, otherwise an error code
 */
int Read_Node_Spec_Conf (char *File_Name) {
    FILE *Node_Spec_File;	/* Pointer to input data file */
    int Error_Code;		/* Error returns from system functions */
    int Line_Num;		/* Line number in input file */
    char In_Line[BUF_SIZE];	/* Input line */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    char My_Name[MAX_NAME_LEN];
    char My_OS[MAX_OS_LEN];
    int My_CPUs;
    float My_Speed;
    int My_RealMemory;
    int My_VirtualMemory;
    long My_TmpDisk;
    unsigned int My_Pool;
    enum Node_State My_NodeState;
    time_t My_LastResponse;

    int Set_CPUs, Set_Speed, Set_RealMemory, Set_VirtualMemory, Set_TmpDisk;
    int Set_Pool, Set_State, Set_LastResponse;

    /* Initialization */
    Error_Code = 0;
    Node_Spec_File = fopen(File_Name, "r");
    if (Node_Spec_File == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Read_Node_Spec_Conf error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ALERT, "Read_Node_Spec_Conf error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */
    strcpy(Default_Record.Name, "DEFAULT");
    strcpy(Default_Record.OS, "UNKNOWN");
    Default_Record.CPUs = 1;
    Default_Record.Speed = 1.0;
    Default_Record.RealMemory = 0;
    Default_Record.VirtualMemory = 0;
    Default_Record.TmpDisk = 0L;
    Default_Record.Pool = 0;
    Default_Record.NodeState= STATE_UNKNOWN;
    Default_Record.LastResponse = 0;
    Node_Record_List = list_create(NULL);

    /* Process the data file */
    Line_Num = 0;
    while (fgets(In_Line, BUF_SIZE, Node_Spec_File) != NULL) {
	Line_Num++;
	if (strlen(In_Line) >= (BUF_SIZE-1)) {
#ifdef DEBUG_MODULE
	    fprintf(stderr, "Read_Node_Spec_Conf line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#else
	    syslog(LOG_ALERT, "Read_Node_Spec_Conf line %d, of input file %s too long\n", 
		Line_Num, File_Name);
#endif
	    Error_Code = E2BIG;
	    break;
	}
	if (In_Line[0] == '#') continue;
	Error_Code = Parse_Node_Spec(In_Line, My_Name, My_OS, 
	    &My_CPUs, &Set_CPUs, &My_Speed, &Set_Speed,
	    &My_RealMemory, &Set_RealMemory, &My_VirtualMemory, &Set_VirtualMemory, 
	    &My_TmpDisk, &Set_TmpDisk, &My_Pool, &Set_Pool, &My_NodeState, &Set_State,
	    &My_LastResponse, &Set_LastResponse);
	if (Error_Code != 0) break;
	if (strcmp("DEFAULT", My_Name) == 0) {
	    if (strlen(My_OS) != 0)     strcpy(Default_Record.OS, My_OS);
	    if (Set_CPUs != 0)          Default_Record.CPUs=My_CPUs;
	    if (Set_Speed != 0)         Default_Record.Speed=My_Speed;
	    if (Set_RealMemory != 0)    Default_Record.RealMemory=My_RealMemory;
	    if (Set_VirtualMemory != 0) Default_Record.VirtualMemory=My_VirtualMemory;
	    if (Set_TmpDisk != 0)       Default_Record.TmpDisk=My_TmpDisk;
	    if (Set_Pool != 0)          Default_Record.Pool=My_Pool;
	    if (Set_State != 0)         Default_Record.NodeState=My_NodeState;
	} else {
	    Node_Record_Point = Duplicate_Record(Node_Record_Read.Name);
	    if (Node_Record_Point == NULL) {
		Node_Record_Point = (struct Node_Record *)malloc(sizeof(struct Node_Record));
		if (Node_Record_Point == NULL) {
#ifdef DEBUG_MODULE
		    fprintf(stderr, "Read_Node_Spec_Conf malloc failure\n");
#else
		    syslog(LOG_ALERT, "Read_Node_Spec_Conf malloc failure\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		if (list_append(Node_Record_List, (void *)Node_Record_Point) == NULL) {
#ifdef DEBUG_MODULE
		    fprintf(stderr, "Read_Node_Spec_Conf list_append can not allocate memory\n");
#else
		    syslog(LOG_ALERT, "Read_Node_Spec_Conf list_append can not allocate memory\n");
#endif
		    Error_Code =  errno;
		    break;
		} /* if */
		strcpy(Node_Record_Point->Name, My_Name);
		strcpy(Node_Record_Point->OS, Default_Record.OS);
		Node_Record_Point->CPUs          = Default_Record.CPUs;
		Node_Record_Point->Speed         = Default_Record.Speed;
		Node_Record_Point->RealMemory    = Default_Record.RealMemory;
		Node_Record_Point->VirtualMemory = Default_Record.VirtualMemory;
		Node_Record_Point->TmpDisk       = Default_Record.TmpDisk;
		Node_Record_Point->Pool          = Default_Record.Pool;
		Node_Record_Point->NodeState     = Default_Record.NodeState;
	    } else {
#ifdef DEBUG_MODULE
		fprintf(stderr, "Read_Node_Spec_Conf duplicate data for %s, using latest information\n", 
		    Node_Record_Read.Name);
#else
		syslog(LOG_NOTICE, "Read_Node_Spec_Conf duplicate data for %s, using latest information\n", 
		    Node_Record_Read.Name);
#endif
	    } /* else */
	    if (strlen(My_OS) != 0)     strcpy(Node_Record_Point->OS, My_OS);
	    if (Set_CPUs != 0)          Node_Record_Point->CPUs=My_CPUs;
	    if (Set_Speed != 0)         Node_Record_Point->Speed=My_Speed;
	    if (Set_RealMemory != 0)    Node_Record_Point->RealMemory=My_RealMemory;
	    if (Set_VirtualMemory != 0) Node_Record_Point->VirtualMemory=My_VirtualMemory;
	    if (Set_TmpDisk != 0)       Node_Record_Point->TmpDisk=My_TmpDisk;
	    if (Set_Pool != 0)          Node_Record_Point->Pool=My_Pool;
	    if (Set_State != 0)         Node_Record_Point->NodeState=My_Pool;
	} /* else */
    } /* while */

    /* Termination */
    if (fclose(Node_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#ifdef DEBUG_MODULE
	fprintf(stderr, "Read_Node_Spec_Conf error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Read_Node_Spec_Conf error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    return Error_Code;
} /* Read_Node_Spec_Conf */


/*
 * Show_Node_Record - Dump the record for the specified node
 * Input: Node_Name - Name of the node for which data is requested
 *        Node_Record - Location into which the information is written, should be at least BUF_SIZE bytes long
 * Output: Node_Record is filled
 *         return - 0 if no error, otherwise errno
 */
    int Show_Node_Record (char *Node_Name, char *Node_Record) {
    struct Node_Record *Node_Record_Point;
    char Out_Pool[MAX_POOLS*3], Out_Time[20];
    struct tm *Node_Time;

    Node_Record_Point = Duplicate_Record(Node_Name);
    if (Node_Record_Point == NULL) return ENOENT;
    Pool_Value_To_String(Node_Record_Point->Pool, Out_Pool, sizeof(Out_Pool), Node_Record_Point->Name);
/* Alternate, human readable, formatting shown below and commented out */
/*    Node_Time = localtime(&Node_Record_Point->LastResponse); */
/*    strftime(Out_Time, sizeof(Out_Time), "%a%d%b@%H:%M:%S", Node_Time); */
    sprintf(Out_Time, "%ld", Node_Record_Point->LastResponse);
    if (sprintf(Node_Record, 
	  "Name=%s OS=%s CPUs=%d Speed=%f RealMemory=%d VirtualMemory=%d TmpDisk=%ld Pool=%s State=%s LastResponse=%s",
	  Node_Record_Point->Name, Node_Record_Point->OS, Node_Record_Point->CPUs, 
	  Node_Record_Point->Speed, Node_Record_Point->RealMemory, 
	  Node_Record_Point->VirtualMemory, Node_Record_Point->TmpDisk, Out_Pool,
	  Node_State_String[Node_Record_Point->NodeState], Out_Time) == EOF) {
	return EINVAL;
    } /* if */
    return 0;
} /* Show_Node_Record */

/*
 * Update_Node_Spec_Conf - Update the configuration for the given node, create record as needed 
 *	NOTE: To delete a record, specify CPUs=0 in the configuration
 * Input: Specification - Standard configuration file input line
 * Output: return - 0 if no error, otherwise errno
 */
int Update_Node_Spec_Conf (char *Specification) {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    char My_Name[MAX_NAME_LEN];
    char My_OS[MAX_OS_LEN];
    int My_CPUs;
    float My_Speed;
    int My_RealMemory;
    int My_VirtualMemory;
    long My_TmpDisk;
    unsigned int My_Pool;
    enum Node_State My_State;
    time_t My_LastResponse;

    int Set_CPUs, Set_Speed, Set_RealMemory, Set_VirtualMemory, Set_TmpDisk;
    int Set_Pool, Set_State, Set_LastResponse;
    int Error_Code;

    Error_Code = Parse_Node_Spec(Specification, My_Name, My_OS, 
	&My_CPUs, &Set_CPUs, &My_Speed, &Set_Speed,
	&My_RealMemory, &Set_RealMemory, &My_VirtualMemory, &Set_VirtualMemory, 
	&My_TmpDisk, &Set_TmpDisk, &My_Pool, &Set_Pool, &My_State, &Set_State, 
	&My_LastResponse, &Set_LastResponse);
    if (Error_Code != 0) return EINVAL;

    if (strlen(My_Name) == 0) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Update_Node_Spec_Conf invalid input: %s\n", Specification);
#else
	syslog(LOG_ERR, "Update_Node_Spec_Conf invalid input: %s\n", Specification);
#endif
	return EINVAL;
    } /* if */

    Node_Record_Point = Duplicate_Record(My_Name);
    if (Node_Record_Point == NULL) {		/* Create new record as needed */
	Node_Record_Point = (struct Node_Record *)malloc(sizeof(struct Node_Record));
	if (Node_Record_Point == NULL) {
#ifdef DEBUG_MODULE
	    fprintf(stderr, "Update_Node_Spec_Conf malloc failure\n");
#else
	    syslog(LOG_ERR, "Update_Node_Spec_Conf malloc failure\n");
#endif
	    return errno;
	} /* if */
	if (list_append(Node_Record_List, (void *)Node_Record_Point) == NULL) {
#ifdef DEBUG_MODULE
	    fprintf(stderr, "Update_Node_Spec_Conf list_append can not allocate memory\n");
#else
	    syslog(LOG_ERR, "Update_Node_Spec_Conf list_append can not allocate memory\n");
#endif
	    return errno;
	} /* if */

	/* Set defaults */
	strcpy(Node_Record_Point->OS, "UNKNOWN");
	Node_Record_Point->CPUs = 1;
	Node_Record_Point->Speed = 1.0;
	Node_Record_Point->RealMemory = 0;
	Node_Record_Point->VirtualMemory = 0;
	Node_Record_Point->TmpDisk = 0L;
	Node_Record_Point->Pool = 1;
	Node_Record_Point->NodeState = STATE_UNKNOWN;
	Node_Record_Point->LastResponse = 0;

    } /* if */

    
    if ((Set_CPUs != 0) && (My_CPUs == 0)) {	/* Delete record */
	return Delete_Record(My_Name);
	return 0;
    } /* if */

    strcpy(Node_Record_Point->Name, My_Name);
    if (strlen(My_OS) != 0)     strcpy(Node_Record_Point->OS, My_OS);
    if (Set_CPUs != 0)          Node_Record_Point->CPUs=My_CPUs;
    if (Set_Speed != 0)         Node_Record_Point->Speed=My_Speed;
    if (Set_RealMemory != 0)    Node_Record_Point->RealMemory=My_RealMemory;
    if (Set_VirtualMemory != 0) Node_Record_Point->VirtualMemory=My_VirtualMemory;
    if (Set_TmpDisk != 0)       Node_Record_Point->TmpDisk=My_TmpDisk;
    if (Set_Pool != 0)          Node_Record_Point->Pool=My_Pool;
    if (Set_State != 0)         Node_Record_Point->NodeState=My_State;
    if (Set_LastResponse != 0)  Node_Record_Point->LastResponse=My_LastResponse;

    return 0;
} /* Update_Node_Spec_Conf */


/* 
 * Validate_Node_Spec - Determine if the supplied node specification satisfies 
 *	the node record specification (all values at least as high). Note we 
 *	ignore pool and the OS level strings are just run through strcmp
 * Output: Returns 0 if satisfactory, errno otherwise
 */
int Validate_Node_Spec (char *Specification) { 
    int Error_Code;
    struct Node_Record *Node_Record_Point;
    char My_Name[MAX_NAME_LEN];
    char My_OS[MAX_OS_LEN];
    int My_CPUs;
    float My_Speed;
    int My_RealMemory;
    int My_VirtualMemory;
    long My_TmpDisk;
    unsigned My_Pool;
    enum Node_State My_NodeState;
    time_t My_LastResponse;
    int Set_CPUs, Set_Speed, Set_RealMemory, Set_VirtualMemory, Set_TmpDisk;
    int Set_Pool, Set_State, Set_LastResponse;

    Error_Code = Parse_Node_Spec(Specification, My_Name, My_OS, 
	&My_CPUs, &Set_CPUs, &My_Speed, &Set_Speed,
	&My_RealMemory, &Set_RealMemory, &My_VirtualMemory, &Set_VirtualMemory, 
	&My_TmpDisk, &Set_TmpDisk, &My_Pool, &Set_Pool, &My_NodeState, &Set_State,
	&My_LastResponse, &Set_LastResponse);
    if (Error_Code != 0) return Error_Code;
    if (My_Name[0] == (char)NULL) return EINVAL;

    Node_Record_Point = Duplicate_Record(My_Name);
    if (Node_Record_Point == NULL) return ENOENT;
    if ((strlen(My_OS) != 0) && 
	(strcmp(Node_Record_Point->OS, My_OS) < 0)) return EINVAL;
    if ((Set_CPUs != 0) && 
	(Node_Record_Point->CPUs > My_CPUs)) return EINVAL;
    if ((Set_Speed != 0)&& 
	(Node_Record_Point->Speed > My_Speed)) return EINVAL;
    if ((Set_RealMemory != 0) && 
	(Node_Record_Point->RealMemory > My_RealMemory)) return EINVAL;
    if ((Set_VirtualMemory != 0) && 
	(Node_Record_Point->VirtualMemory > My_VirtualMemory)) return EINVAL;
    if ((Set_TmpDisk != 0) && 
	(Node_Record_Point->TmpDisk > My_TmpDisk)) return EINVAL;
    return 0;
} /* Validate_Node_Spec */


/*
 * Write_Node_Spec_Conf - Dump the node specification information into the specified file 
 * Input: File_Name - Name of the file into which the node specification is to be written
 *        Full_Dump - Full node record dump if equal to zero
 * Output: return - 0 if no error, otherwise an error code
 */
int Write_Node_Spec_Conf (char *File_Name, int Full_Dump) {
    FILE *Node_Spec_File;	/* Pointer to output data file */
    int Error_Code;		/* Error returns from system functions */
    char Out_Line[MAX_POOLS*4];	/* Temporary output information storage */
    char Out_Buf[BUF_SIZE];	/* Temporary output information storage */
    int i;			/* Counter */
    time_t now;			/* Current time */
    ListIterator Node_Record_Iterator;		/* For iterating through Node_Record_List */
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    /* Initialization */
    Error_Code = 0;
    Node_Record_Iterator = list_iterator_create(Node_Record_List);
   if (Node_Record_Iterator == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Write_Node_Spec_Conf: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Write_Node_Spec_Conf: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    }
    Node_Spec_File = fopen(File_Name, "w");
    if (Node_Spec_File == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Write_Node_Spec_Conf error %d opening file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Write_Node_Spec_Conf error %d opening file %s\n", errno, File_Name);
#endif
	return errno;
    } /* if */
    (void) time(&now);
    if (fprintf(Node_Spec_File, "#\n# Written by SLURM: %s#\n", ctime(&now)) <= 0) {
	Error_Code = errno;
#ifdef DEBUG_MODULE
	fprintf(stderr, "Write_Node_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#else
	syslog(LOG_ERR, "Write_Node_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#endif
    } /* if */

    /* Process the data file */
    while (Node_Record_Point = (struct Node_Record *)list_next(Node_Record_Iterator)) {
	Pool_Value_To_String(Node_Record_Point->Pool, Out_Line, sizeof(Out_Line), Node_Record_Point->Name);
	if (Full_Dump == 1) {
	    sprintf(Out_Buf, "State=%s, LastResponse=%ld\n", 
		Node_State_String[Node_Record_Point->NodeState], Node_Record_Point->LastResponse); 
	} else {
	    strcpy(Out_Buf, "\n"); 
	} /* else */
        if (fprintf(Node_Spec_File, 
	  "Name=%s OS=%s CPUs=%d Speed=%f RealMemory=%d VirtualMemory=%d TmpDisk=%ld Pool=%s %s",
	  Node_Record_Point->Name, Node_Record_Point->OS, Node_Record_Point->CPUs, 
	  Node_Record_Point->Speed, Node_Record_Point->RealMemory, 
	  Node_Record_Point->VirtualMemory, Node_Record_Point->TmpDisk, Out_Line, Out_Buf) <= 0) {
	    if (Error_Code == 0) Error_Code = errno;
#ifdef DEBUG_MODULE
	    fprintf(stderr, "Write_Node_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#else
	    syslog(LOG_ERR, "Write_Node_Spec_Conf error %d printing to file %s\n", errno, File_Name);
#endif
	} /* if */
    } /* while */

    /* Termination */
    if (fclose(Node_Spec_File) != 0) {
	if (Error_Code == 0) Error_Code = errno;
#ifdef DEBUG_MODULE
	fprintf(stderr, "Write_Node_Spec_Conf error %d closing file %s\n", errno, File_Name);
#else
	syslog(LOG_NOTICE, "Write_Node_Spec_Conf error %d closing file %s\n", errno, File_Name);
#endif
    } /* if */
    list_iterator_destroy(Node_Record_Iterator);
    return Error_Code;
} /* Write_Node_Spec_Conf */
