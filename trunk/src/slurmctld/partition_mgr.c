/* 
 * partition_mgr.c - Manage the partition information of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define NO_VAL   -99
#define SEPCHARS " \n\t"

struct	Part_Record Default_Part;		/* Default configuration values */
List	Part_List = NULL;			/* Partition List */
char	Default_Part_Name[MAX_NAME_LEN];	/* Name of default partition */
struct	Part_Record *Default_Part_Loc = NULL;	/* Location of default partition */
time_t	Last_Part_Update;			/* Time of last update to Part Records */
static pthread_mutex_t Part_Mutex= PTHREAD_MUTEX_INITIALIZER;	/* Lock for partition info */

int 	Build_Part_BitMap(struct Part_Record *Part_Record_Point);
void	List_Delete_Part(void *Part_Entry);
int	List_Find_Part(void *Part_Entry, void *key);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    time_t Update_Time;
    struct Part_Record *Part_Ptr;
    char *Dump;
    int Dump_Size;
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int MaxTime;		/* -1 if unlimited */
    int MaxNodes;		/* -1 if unlimited */
    int TotalNodes;		/* Total number of nodes in the partition */
    int TotalCPUs;		/* Total number of CPUs in the partition */
    char *Nodes;		/* Names of nodes in partition */
    char *AllowGroups;		/* NULL indicates ALL */
    int Key;    	 	/* 1 if SLURM distributed key is required for use of partition */
    int StateUp;		/* 1 if state is UP */
    int Shared;			/* 1 if partition can be shared */
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
    int BitMapSize;		/* Bytes in NodeBitMap */
    char Update_Spec[] = "MaxTime=34 MaxNodes=56 Key=NO State=DOWN Shared=FORCE";

    Error_Code = Init_Node_Conf();
    if (Error_Code) printf("Init_Node_Conf error %d\n", Error_Code);
    Part_Lock();
    Error_Code = Init_Part_Conf();
    if (Error_Code) printf("Init_Part_Conf error %d\n", Error_Code);
    Default_Part.MaxTime	= 223344;
    Default_Part.MaxNodes	= 556677;
    Default_Part.TotalNodes	= 4;
    Default_Part.TotalCPUs	= 16;
    Default_Part.Key	   	= 1;
    Node_Record_Count 		= 8;

    printf("Create some partitions and test defaults\n");
    Part_Ptr = Create_Part_Record(&Error_Code);
    if (Error_Code) 
	printf("Create_Part_Record error %d\n", Error_Code);
    else {
	static int Tmp_BitMap;
	if (Part_Ptr->MaxTime  != 223344) printf("ERROR: Partition default MaxTime not set\n");
	if (Part_Ptr->MaxNodes != 556677) printf("ERROR: Partition default MaxNodes not set\n");
	if (Part_Ptr->TotalNodes != 4)    printf("ERROR: Partition default TotalNodes not set\n");
	if (Part_Ptr->TotalCPUs != 16)    printf("ERROR: Partition default MaxNodes not set\n");
	if (Part_Ptr->Key != 1)           printf("ERROR: Partition default Key not set\n");
	if (Part_Ptr->StateUp != 1)       printf("ERROR: Partition default StateUp not set\n");
	if (Part_Ptr->Shared != 0)        printf("ERROR: Partition default Shared not set\n");
	strcpy(Part_Ptr->Name, "Interactive");
	Part_Ptr->Nodes = "lx[01-04]";
	Part_Ptr->AllowGroups = "students";
	Tmp_BitMap = 0x3c << (sizeof(unsigned)*8-8);
	Part_Ptr->NodeBitMap = &Tmp_BitMap;
    } /* else */
    Part_Ptr = Create_Part_Record(&Error_Code);
    if (Error_Code) 
	printf("Create_Part_Record error %d\n", Error_Code);
    else 
	strcpy(Part_Ptr->Name, "Batch");
    Part_Ptr = Create_Part_Record(&Error_Code);
    if (Error_Code) 
	printf("ERROR: Create_Part_Record error %d\n", Error_Code);
    else 
	strcpy(Part_Ptr->Name, "Class");

    Update_Time = (time_t)0;
    Error_Code = Dump_Part(&Dump, &Dump_Size, &Update_Time);
    if (Error_Code) printf("ERROR: Dump_Part error %d\n", Error_Code);

    Error_Code = Update_Part("Batch", Update_Spec);
    if (Error_Code) printf("ERROR: Update_Part error %d\n", Error_Code);

    Part_Ptr   = list_find_first(Part_List, &List_Find_Part, "Batch");
    if (Part_Ptr == NULL) printf("ERROR: list_find failure\n");
    if (Part_Ptr->MaxTime  != 34) printf("ERROR: Update_Part MaxTime not reset\n");
    if (Part_Ptr->MaxNodes != 56) printf("ERROR: Update_Part MaxNodes not reset\n");
    if (Part_Ptr->Key != 0)       printf("ERROR: Update_Part Key not reset\n");
    if (Part_Ptr->StateUp != 0)   printf("ERROR: Update_Part StateUp not set\n");
    if (Part_Ptr->Shared != 2)    printf("ERROR: Update_Part Shared not set\n");

    Node_Record_Count = 0;	/* Delete_Part_Record dies if node count is bad */
    Error_Code = Delete_Part_Record("Batch");
    if (Error_Code != 0)  printf("Delete_Part_Record error1 %d\n", Error_Code);
    printf("NOTE: We expect Delete_Part_Record to report not finding a record for Batch\n");
    Error_Code = Delete_Part_Record("Batch");
    if (Error_Code != ENOENT)  printf("ERROR: Delete_Part_Record error2 %d\n", Error_Code);
    Part_Unlock();

    exit(0);
} /* main */
#endif


/*
 * Build_Part_BitMap - Update the TotalCPUs, TotalNodes, and NodeBitMap for the specified partition
 *	Also reset the partition pointers in the node back to this partition.
 * Input: Part_Record_Point - Pointer to the partition
 * Output: Returns 0 if no error, errno otherwise
 * NOTE: This does not report nodes defined in more than one partition. This is checked only  
 *	upon reading the configuration file, not on an update
 */
int Build_Part_BitMap(struct Part_Record *Part_Record_Point) {
    int Start_Inx, End_Inx, Count_Inx;
    int i, j, Error_Code, size;
    char *str_ptr1, *str_ptr2, *Format, *My_Node_List, This_Node_Name[BUF_SIZE];
    unsigned *Old_BitMap;
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    Format = My_Node_List = NULL;
    Part_Record_Point->TotalCPUs  = 0;
    Part_Record_Point->TotalNodes = 0;
    
    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 
		(sizeof(unsigned)*8); 	/* Unsigned int records in bitmap */
    size *= 8;				/* Bytes in bitmap */
    if (Part_Record_Point->NodeBitMap == NULL) {
	Part_Record_Point->NodeBitMap = malloc(size);
	if (Part_Record_Point->NodeBitMap == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Build_Part_BitMap: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Build_Part_BitMap: unable to allocate memory\n");
#endif
	    abort();
	} /* if */
	Old_BitMap = NULL;
    } else
	Old_BitMap = BitMapCopy(Part_Record_Point->NodeBitMap);
    memset(Part_Record_Point->NodeBitMap, 0, size);

    if (Part_Record_Point->Nodes == NULL) {
	if (Old_BitMap) free(Old_BitMap);
	return 0;
    } /* if */
    My_Node_List = malloc(strlen(Part_Record_Point->Nodes)+1);
    if (My_Node_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Build_Part_BitMap: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Build_Part_BitMap: unable to allocate memory\n");
#endif
	if (Old_BitMap) free(Old_BitMap);
	abort();
    } /* if */
    strcpy(My_Node_List, Part_Record_Point->Nodes);

    str_ptr2 = (char *)strtok_r(My_Node_List, ",", &str_ptr1);
    while (str_ptr2) {	/* Break apart by comma separators */
	Error_Code = Parse_Node_Name(str_ptr2, &Format, &Start_Inx, &End_Inx, &Count_Inx);
	if (Error_Code) {
	    free(My_Node_List);
	    if (Old_BitMap) free(Old_BitMap);
	    return EINVAL;
	} /* if */
	if (strlen(Format) >= sizeof(This_Node_Name)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Build_Part_BitMap: Node name specification too long: %s\n", Format);
#else
	    syslog(LOG_ERR, "Build_Part_BitMap: Node name specification too long: %s\n", Format);
#endif
	    free(My_Node_List);
	    free(Format);
	    if (Old_BitMap) free(Old_BitMap);
	    return EINVAL;
	} /* if */
	for (i=Start_Inx; i<=End_Inx; i++) {
	    if (Count_Inx == 0) 
		strncpy(This_Node_Name, Format, sizeof(This_Node_Name));
	    else
		sprintf(This_Node_Name, Format, i);
	    Node_Record_Point = Find_Node_Record(This_Node_Name);
	    if (Node_Record_Point == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Build_Part_BitMap: Invalid node specified %s\n", This_Node_Name);
#else
		syslog(LOG_ERR, "Build_Part_BitMap: Invalid node specified %s\n", This_Node_Name);
#endif
		free(My_Node_List);
		free(Format);
		if (Old_BitMap) free(Old_BitMap);
		return EINVAL;
	    } /* if */
	    BitMapSet(Part_Record_Point->NodeBitMap, 
			(int)(Node_Record_Point - Node_Record_Table_Ptr));
	    Part_Record_Point->TotalNodes++;
	    Part_Record_Point->TotalCPUs += Node_Record_Point->CPUs;
	    Node_Record_Point->Partition_Ptr = Part_Record_Point;
	    BitMapClear(Old_BitMap, (int)(Node_Record_Point - Node_Record_Table_Ptr));
	} /* for */
	str_ptr2 = (char *)strtok_r(NULL, ",", &str_ptr1);
    } /* while */

    /* Unlink nodes removed from the partition */
    for (i=0; i<Node_Record_Count; i++) {
	if (BitMapValue(Old_BitMap, i) == 0) continue;
	Node_Record_Table_Ptr[i].Partition_Ptr = NULL;
    } /* for */

    if(My_Node_List) free(My_Node_List);
    if(Format) free(Format);
    if (Old_BitMap) free(Old_BitMap);
    return 0;
} /* Build_Part_BitMap */


/* 
 * Create_Part_Record - Create a partition record
 * Input: Error_Code - Location to store error value in
 * Output: Error_Code - Set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE: The record's values are initialized to those of Default_Part
 * NOTE: Allocates memory that should be freed with Delete_Part_Record
 */
struct Part_Record *Create_Part_Record(int *Error_Code) {
    struct Part_Record *Part_Record_Point;

    *Error_Code = 0;
    Last_Part_Update = time(NULL);

    Part_Record_Point = (struct Part_Record *)malloc(sizeof(struct Part_Record));
    if (Part_Record_Point == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n");
#endif
	abort();
    } /* if */

    strcpy(Part_Record_Point->Name, "DEFAULT");
    Part_Record_Point->MaxTime     = Default_Part.MaxTime;
    Part_Record_Point->MaxNodes    = Default_Part.MaxNodes;
    Part_Record_Point->Key         = Default_Part.Key;
    Part_Record_Point->StateUp     = Default_Part.StateUp;
    Part_Record_Point->Shared      = Default_Part.Shared;
    Part_Record_Point->TotalNodes  = Default_Part.TotalNodes;
    Part_Record_Point->TotalCPUs   = Default_Part.TotalCPUs;
    Part_Record_Point->NodeBitMap  = NULL;
#if DEBUG_SYSTEM
    Part_Record_Point->Magic       = PART_MAGIC;
#endif

    if (Default_Part.AllowGroups) {
	Part_Record_Point->AllowGroups = (char *)malloc(strlen(Default_Part.AllowGroups)+1);
	if (Part_Record_Point->AllowGroups == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n");
#endif
	    abort();
	} /* if */
	strcpy(Part_Record_Point->AllowGroups, Default_Part.AllowGroups);
    } else
	Part_Record_Point->AllowGroups = NULL;

    if (Default_Part.Nodes) {
	Part_Record_Point->Nodes = (char *)malloc(strlen(Default_Part.Nodes)+1);
	if (Part_Record_Point->Nodes == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n");
#endif
	    abort();
	} /* if */
	strcpy(Part_Record_Point->Nodes, Default_Part.Nodes);
    } else
	Part_Record_Point->Nodes = NULL;

    if (list_append(Part_List, Part_Record_Point) == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Part_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Part_Record: unable to allocate memory\n");
#endif
	abort();
    } /* if */

    return Part_Record_Point;
} /* Create_Part_Record */


/* 
 * Delete_Part_Record - Delete record for partition with specified name
 * Input: name - Name of the desired node, Delete all partitions if pointer is NULL 
 * Output: return 0 on success, errno otherwise
 */
int Delete_Part_Record(char *name) {
    int i;

    Last_Part_Update = time(NULL);
    if (name == NULL) 
	i = list_delete_all(Part_List, &List_Find_Part, "UNIVERSAL_KEY");
    else
	i = list_delete_all(Part_List, &List_Find_Part, name);
    if ((name == NULL) || (i != 0)) return 0;

#if DEBUG_SYSTEM
    fprintf(stderr, "Delete_Part_Record: Attempt to delete non-existent partition %s\n", name);
#else
    syslog(LOG_ERR, "Delete_Part_Record: Attempt to delete non-existent partition %s\n", name);
#endif
    return ENOENT;
} /* Delete_Part_Record */


/* 
 * Dump_Part - Dump all partition information to a buffer
 * Input: Buffer_Ptr - Location into which a pointer to the data is to be stored.
 *                     The data buffer is actually allocated by Dump_Part and the 
 *                     calling function must free the storage.
 *         Buffer_Size - Location into which the size of the created buffer is in bytes
 *         Update_Time - Dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * Output: Buffer_Ptr - The pointer is set to the allocated buffer.
 *         Buffer_Size - Set to size of the buffer in bytes
 *         Update_Time - set to time partition records last updated
 *         Returns 0 if no error, errno otherwise
 * NOTE: The buffer at *Buffer_Ptr must be freed by the caller
 * NOTE: IF YOU MAKE ANY CHANGES HERE be sure to increment the value of PART_STRUCT_VERSION
 *       and make the corresponding changes to Load_Part_Name in api/partition_info.c
 */
int Dump_Part(char **Buffer_Ptr, int *Buffer_Size, time_t *Update_Time) {
    ListIterator Part_Record_Iterator;		/* For iterating through Part_Record_List */
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */
    char *Buffer;
    int Buffer_Offset, Buffer_Allocated, i, Record_Size;
    char Out_Line[BUF_SIZE*2], *Nodes, *Key, *Default, *AllowGroups, *Shared, *State;

    Buffer_Ptr[0] = NULL;
    *Buffer_Size = 0;
    Buffer = NULL;
    Buffer_Offset = 0;
    Buffer_Allocated = 0;
   if (*Update_Time == Last_Part_Update) return 0;

    Part_Lock();
    Part_Record_Iterator = list_iterator_create(Part_List);
    if (Part_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Part: list_iterator_create unable to allocate memory\n");
#endif
	abort();
    } /* if */

    /* Write haeader, version and time */
    sprintf(Out_Line, HEAD_FORMAT, (unsigned long)Last_Part_Update, PART_STRUCT_VERSION);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    /* Write partition records */
    while (Part_Record_Point = (struct Part_Record *)list_next(Part_Record_Iterator)) {
#if DEBUG_SYSTEM
	if (Part_Record_Point->Magic != PART_MAGIC) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Dump_Part: Data integrity is bad\n");
#else
	    syslog(LOG_ALERT, "Dump_Part: Data integrity is bad\n");
#endif
	    abort();
	} /* if */
#endif

	if (Part_Record_Point->Nodes)
	    Nodes = Part_Record_Point->Nodes;
	else
	    Nodes = "NONE";
	if (Part_Record_Point == Default_Part_Loc)
	    Default = "YES";
	else
	    Default = "NO";
	if (Part_Record_Point->Key)
	    Key = "YES";
	else
	    Key = "NO";
	if (Part_Record_Point->StateUp)
	    State = "UP";
	else
	    State = "DOWN";
	if (Part_Record_Point->Shared)
	    Shared = "YES";
	else
	    Shared = "NO";
	if (Part_Record_Point->AllowGroups)
	    AllowGroups = Part_Record_Point->AllowGroups;
	else
	    AllowGroups = "ALL";
	sprintf(Out_Line, PART_STRUCT_FORMAT, 
		Part_Record_Point->Name,
		Part_Record_Point->MaxNodes,
		Part_Record_Point->MaxTime,
		Nodes,
		Key,
		Default,
		AllowGroups,
		Shared,
		State,
		Part_Record_Point->TotalNodes,
		Part_Record_Point->TotalCPUs);

	if (strlen(Out_Line) > BUF_SIZE) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Dump_Part: buffer overflow for partition %s\n", Part_Record_Point->Name);
#else
	    syslog(LOG_ALERT, "Dump_Part: buffer overflow for partition %s\n", Part_Record_Point->Name);
#endif
	    if (strlen(Out_Line) > (2*BUF_SIZE)) abort();
	} /* if */

	if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;
    } /* while */

    list_iterator_destroy(Part_Record_Iterator);
    Buffer = realloc(Buffer, Buffer_Offset);
    if (Buffer == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Part: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Part: unable to allocate memory\n");
#endif
	abort();
    } /* if */

    Buffer_Ptr[0] = Buffer;
    *Buffer_Size = Buffer_Offset;
    *Update_Time = Last_Part_Update;
    Part_Unlock();
    return 0;

cleanup:
    Part_Unlock();
    list_iterator_destroy(Part_Record_Iterator);
    if (Buffer) free(Buffer);
    return EINVAL;
} /* Dump_Part */


/* 
 * Init_Part_Conf - Initialize the partition configuration values. 
 * This should be called before creating any partition entries.
 * Output: return value - 0 if no error, otherwise an error code
 */
int Init_Part_Conf() {
    Last_Part_Update = time(NULL);

    strcpy(Default_Part.Name, "DEFAULT");
    Default_Part.AllowGroups = (char *)NULL;
    Default_Part.MaxTime     = -1;
    Default_Part.MaxNodes    = -1;
    Default_Part.Key         = 0;
    Default_Part.StateUp     = 1;
    Default_Part.Shared      = 0;
    Default_Part.TotalNodes  = 0;
    Default_Part.TotalCPUs   = 0;
    if (Default_Part.Nodes) free(Default_Part.Nodes);
    Default_Part.Nodes       = (char *)NULL;
    if (Default_Part.AllowGroups) free(Default_Part.AllowGroups);
    Default_Part.AllowGroups = (char *)NULL;
    if (Default_Part.NodeBitMap) free(Default_Part.NodeBitMap);
    Default_Part.NodeBitMap  = (unsigned *)NULL;

    if (Part_List) 	/* Delete defunct partitions */
	(void)Delete_Part_Record(NULL);
    else
	Part_List = list_create(&List_Delete_Part);

    if (Part_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_Part_Conf: list_create can not allocate memory\n");
#else
	syslog(LOG_ALERT, "Init_Part_Conf: list_create can not allocate memory\n");
#endif
	abort();
    } /* if */

    strcpy(Default_Part_Name, "");
    Default_Part_Loc = (struct Part_Record *)NULL;

    return 0;
} /* Init_Part_Conf */


/* List_Delete_Part - Delete an entry from the partition list, see list.h for documentation */
void List_Delete_Part(void *Part_Entry) {
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */
    int i;

    Part_Record_Point = (struct Part_Record *)Part_Entry;
    for (i=0; i<Node_Record_Count; i++) {
	if (Node_Record_Table_Ptr[i].Partition_Ptr != Part_Record_Point) continue;
	Node_Record_Table_Ptr[i].Partition_Ptr = NULL;
    } /* if */
    if (Part_Record_Point->AllowGroups) free(Part_Record_Point->AllowGroups);
    if (Part_Record_Point->Nodes)       free(Part_Record_Point->Nodes);
    if (Part_Record_Point->NodeBitMap)  free(Part_Record_Point->NodeBitMap);
    free(Part_Entry);
} /* List_Delete_Part */


/* List_Find_Part - Find an entry in the partition list, see list.h for documentation 
 * Key is partition name or "UNIVERSAL_KEY" for all partitions */
int List_Find_Part(void *Part_Entry, void *key) {
    struct Part_Record *Part_Record_Point;	/* Pointer to Part_Record */
    if (strcmp(key, "UNIVERSAL_KEY") == 0) return 1;
    Part_Record_Point = (struct Part_Record *)Part_Entry;
    if (strcmp(Part_Record_Point->Name, (char *)key) == 0) return 1;
    return 0;
} /* List_Find_Part */


/* Part_Lock - Lock the partition information */
void Part_Lock() {
    int Error_Code;
    Error_Code = pthread_mutex_lock(&Part_Mutex);
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Part_Lock: pthread_mutex_lock error %d\n", Error_Code);
#else
	syslog(LOG_ALERT, "Part_Lock: pthread_mutex_lock error %d\n", Error_Code);
#endif
	abort();
    } /* if */
} /* Part_Lock */


/* Part_Unlock - Unlock the partition information */
void Part_Unlock() {
    int Error_Code;
    Error_Code = pthread_mutex_unlock(&Part_Mutex);
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Part_Unlock: pthread_mutex_unlock error %d\n", Error_Code);
#else
	syslog(LOG_ALERT, "Part_Unlock: pthread_mutex_unlock error %d\n", Error_Code);
#endif
	abort();
    } /* if */
} /* Part_Unlock */


/* 
 * Update_Part - Update a partition's configuration data
 * Input: PartitionName - Partition's name
 *        Spec - The updates to the partition's specification 
 * Output:  Return - 0 if no error, otherwise an error code
 * NOTE: The contents of Spec are overwritten by white space
 */
int Update_Part(char *PartitionName, char *Spec) {
    int Error_Code;
    struct Part_Record *Part_Ptr;
    int MaxTime_Val, MaxNodes_Val, Key_Val, State_Val, Shared_Val, Default_Val;
    char *AllowGroups, *Nodes;
    int Bad_Index, i;

    if (strcmp(PartitionName, "DEFAULT") == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: Invalid partition name %s\n", PartitionName);
#else
	syslog(LOG_ALERT, "Update_Part: Invalid partition name  %s\n", PartitionName);
#endif
	return EINVAL;
    } /* if */

    Node_Lock();
    Part_Lock();
    Part_Ptr   = list_find_first(Part_List, &List_Find_Part, PartitionName);
    if (Part_Ptr == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: Partition %s does not exist, being created.\n", PartitionName);
#else
	syslog(LOG_ALERT, "Update_Part: Partition %s does not exist, being created.\n", PartitionName);
#endif
	Part_Ptr = Create_Part_Record(&Error_Code);
	if (Error_Code) goto cleanup;
    } /* if */

    MaxTime_Val = NO_VAL;
    Error_Code = Load_Integer(&MaxTime_Val, "MaxTime=", Spec);
    if (Error_Code) goto cleanup;

    MaxNodes_Val = NO_VAL;
    Error_Code = Load_Integer(&MaxNodes_Val, "MaxNodes=", Spec);
    if (Error_Code) goto cleanup;

    Key_Val = NO_VAL;
    Error_Code = Load_Integer(&Key_Val, "Key=NO", Spec);
    if (Error_Code) goto cleanup;
    if (Key_Val == 1) Key_Val = 0;
    Error_Code = Load_Integer(&Key_Val, "Key=YES", Spec);
    if (Error_Code) goto cleanup;

    State_Val = NO_VAL;
    Error_Code = Load_Integer(&State_Val, "State=DOWN", Spec);
    if (Error_Code) goto cleanup;
    if (State_Val == 1) State_Val = 0;
    Error_Code = Load_Integer(&State_Val, "State=UP", Spec);
    if (Error_Code) goto cleanup;

    Shared_Val = NO_VAL;
    Error_Code = Load_Integer(&Shared_Val, "Shared=NO", Spec);
    if (Error_Code) goto cleanup;
    if (Shared_Val == 1) Shared_Val = 0;
    Error_Code = Load_Integer(&Shared_Val, "Shared=FORCE", Spec);
    if (Error_Code) goto cleanup;
    if (Shared_Val == 1) Shared_Val = 2;
    Error_Code = Load_Integer(&Shared_Val, "Shared=YES", Spec);
    if (Error_Code) goto cleanup;

    Default_Val = NO_VAL;
    Error_Code = Load_Integer(&Default_Val, "Default=YES", Spec);
    if (Error_Code) goto cleanup;

    AllowGroups = NULL;
    Error_Code = Load_String (&AllowGroups, "AllowGroups=", Spec);
    if (Error_Code) goto cleanup;

    Nodes = NULL;
    Error_Code = Load_String (&Nodes, "Nodes=", Spec);
    if (Error_Code) goto cleanup;

    Bad_Index = -1;
    for (i=0; i<strlen(Spec); i++) {
	if (Spec[i] == '\n') Spec[i]=' ';
	if (isspace((int)Spec[i])) continue;
	Bad_Index=i;
	break;
    } /* if */

    if (Bad_Index != -1) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: Ignored partition %s update specification: %s\n", 
		PartitionName, &Spec[Bad_Index]);
#else
	syslog(LOG_ERR, "Update_Part: Ignored partition %s update specification: %s\n", 
		PartitionName, &Spec[Bad_Index]);
#endif
	Error_Code = EINVAL;
	goto cleanup;
    } /* if */

    Last_Part_Update = time(NULL);
    if (MaxTime_Val  != NO_VAL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: setting MaxTime to %d for partition %s\n", 
		    MaxTime_Val, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: setting MaxTime to %d for partition %s\n", 
		    MaxTime_Val, PartitionName);
#endif
	Part_Ptr->MaxTime  = MaxTime_Val;
    }/* if */

    if (MaxNodes_Val != NO_VAL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: setting MaxNodes to %d for partition %s\n", 
		    MaxNodes_Val, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: setting MaxNodes to %d for partition %s\n", 
		    MaxNodes_Val, PartitionName);
#endif
	Part_Ptr->MaxNodes = MaxNodes_Val;
    }/* if */

    if (Key_Val      != NO_VAL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: setting Key to %d for partition %s\n", 
		    Key_Val, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: setting Key to %d for partition %s\n", 
		    Key_Val, PartitionName);
#endif
	Part_Ptr->Key      = Key_Val;
    }/* if */

    if (State_Val    != NO_VAL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: setting StateUp to %d for partition %s\n", 
		    State_Val, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: setting StateUp to %d for partition %s\n", 
		    State_Val, PartitionName);
#endif
	Part_Ptr->StateUp  = State_Val;
    }/* if */

    if (Shared_Val    != NO_VAL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: setting Shared to %d for partition %s\n", 
		    Shared_Val, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: setting Shared to %d for partition %s\n", 
		    Shared_Val, PartitionName);
#endif
	Part_Ptr->Shared  = Shared_Val;
    }/* if */

    if (Default_Val == 1) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: changing default partition from %s to %s\n", 
		    Default_Part_Name, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: changing default partition from %s to %s\n", 
		    Default_Part_Name, PartitionName);
#endif
	strcpy(Default_Part_Name, PartitionName);
	Default_Part_Loc = Part_Ptr;
    } /* if */

    if (AllowGroups != NULL) {
	if (Part_Ptr->AllowGroups) free(Part_Ptr->AllowGroups);
	Part_Ptr->AllowGroups = AllowGroups;
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: setting AllowGroups to %s for partition %s\n", 
		    AllowGroups, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: setting AllowGroups to %s for partition %s\n", 
		    AllowGroups, PartitionName);
#endif
    } /* if */

    if (Nodes != NULL) {
	if (Part_Ptr->Nodes) free(Part_Ptr->Nodes);
	Part_Ptr->Nodes = Nodes;
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Part: setting Nodes to %s for partition %s\n", 
		    Nodes, PartitionName);
#else
	syslog(LOG_NOTICE, "Update_Part: setting Nodes to %s for partition %s\n", 
		    Nodes, PartitionName);
#endif
	/* Now we need to update TotalCPUs, TotalNodes, and NodeBitMap */
	Error_Code = Build_Part_BitMap(Part_Ptr);
	if (Error_Code) goto cleanup;
    } /* if */

cleanup:
    Part_Unlock();
    Node_Unlock();
    return Error_Code;
} /* Update_Part */
