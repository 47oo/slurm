/* 
 * node_mgr.c - Manage the node records of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM 1
#define PROTOTYPE_API 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif 

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 	1024
#define NO_VAL	 	-99
#define SEPCHARS 	" \n\t"

List 	Config_List = NULL;		/* List of Config_Record entries */
int	Node_Record_Count = 0;		/* Count of records in the Node Record Table */
struct Node_Record *Node_Record_Table_Ptr = NULL; /* Location of the node records */
char 	*Node_State_String[] = {"UNKNOWN", "IDLE", "STAGE_IN", "BUSY", "STAGE_OUT", "DOWN", "DRAINED", "DRAINING", "UP", "END"};
int	*Hash_Table = NULL;		/* Table of hashed indicies into Node_Record */
struct 	Config_Record Default_Config_Record;
struct 	Node_Record   Default_Node_Record;
time_t 	Last_Node_Update =(time_t)NULL;	/* Time of last update to Node Records */

unsigned *Up_NodeBitMap  = NULL;		/* Bitmap of nodes are UP */
unsigned *Idle_NodeBitMap = NULL;	/* Bitmap of nodes are IDLE */

int	Delete_Config_Record();
void	Dump_Hash();
int 	Hash_Index(char *name);
void 	Rehash();

#if PROTOTYPE_API
char *Node_API_Buffer = NULL;
int  Node_API_Buffer_Size = 0;

int Load_Node(char *Buffer, int Buffer_Size);
int Load_Node_Config(int Index, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features,
	char **Nodes, unsigned **NodeBitMap, int *BitMapSize);
int Load_Nodes_Idle(unsigned **NodeBitMap, int *BitMap_Size);
int Load_Node_Name(char *Req_Name, char *Next_Name, int *State, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features);
int Load_Nodes_Up(unsigned **NodeBitMap, int *BitMap_Size);
#endif

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, size;
    char *Out_Line;
    unsigned *Map1, *Map2;
    unsigned U_Map[2];
    struct Config_Record *Config_Ptr;
    struct Node_Record *Node_Ptr;
    char *Format;
    int Start_Inx, End_Inx, Count_Inx;
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int State, CPUs, RealMemory, TmpDisk, Weight;
    char *Features, *Nodes;
    char *Dump;
    int Dump_Size;
    time_t Update_Time;
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
    int BitMapSize;		/* Bytes in NodeBitMap */
    char Update_Spec[] = "State=DRAINING";

    /* Bitmap setup */
    Node_Record_Count = 97;
    size = (Node_Record_Count + 7) / 8;
    Map1 = malloc(size);
    memset(Map1, 0, size);
    BitMapSet(Map1, 7);
    BitMapSet(Map1, 23);
    BitMapSet(Map1, 71);
    Map2 = BitMapCopy(Map1);
    BitMapSet(Map2, 11);
    Node_Record_Count = 0;

    /* Now check out configuration and node structure functions */
    Error_Code = Init_Node_Conf();
    if (Error_Code) printf("ERROR: Init_Node_Conf error %d\n", Error_Code);
    Default_Config_Record.CPUs       = 12;
    Default_Config_Record.RealMemory = 345;
    Default_Config_Record.TmpDisk    = 67;
    Default_Config_Record.Weight     = 89;
    Default_Node_Record.LastResponse = (time_t)678;

    Config_Ptr = Create_Config_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Config_Record error %d\n", Error_Code);
    if (Config_Ptr->CPUs != 12)        printf("ERROR: Config default CPUs not set\n");
    if (Config_Ptr->RealMemory != 345) printf("ERROR: Config default RealMemory not set\n");
    if (Config_Ptr->TmpDisk != 67)     printf("ERROR: Config default TmpDisk not set\n");
    if (Config_Ptr->Weight != 89)      printf("ERROR: Config default Weight not set\n");
    Config_Ptr->Feature = "for_lx01";
    Config_Ptr->Nodes = "lx01";
    Config_Ptr->NodeBitMap = Map1;
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx01");
    Node_Ptr->Config_Ptr = Config_Ptr;
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx02");
    Node_Ptr->Config_Ptr = NULL;
    Error_Code = Update_Node("lx[01-02]", Update_Spec);
    if (Error_Code) printf("ERROR: Update_Node error1 %d\n", Error_Code);
    if (Node_Ptr->NodeState != STATE_DRAINING) 
	printf("ERROR: Update_Node error2 NodeState=%d\n", Node_Ptr->NodeState);
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Node_Record error %d\n", Error_Code);
    Config_Ptr = Create_Config_Record(&Error_Code);
    Config_Ptr->CPUs = 543;
    Config_Ptr->Nodes = "lx[03-20]";
    Config_Ptr->Feature = "for_lx03,lx04";
    Config_Ptr->NodeBitMap = Map2;
    strcpy(Node_Ptr->Name, "lx03");
    if (Node_Ptr->LastResponse != (time_t)678) printf("ERROR: Node default LastResponse not set\n");
    Node_Ptr->Config_Ptr = Config_Ptr;
    Node_Ptr   = Create_Node_Record(&Error_Code);
    if (Error_Code) printf("ERROR: Create_Node_Record error %d\n", Error_Code);
    strcpy(Node_Ptr->Name, "lx04");
    Node_Ptr->Config_Ptr = Config_Ptr;

    Error_Code = NodeName2BitMap("lx[01-02],lx04", &Map1);
    if (Error_Code) printf("ERROR: NodeName2BitMap error %d\n", Error_Code);
    Error_Code = BitMap2NodeName(Map1, &Out_Line);
    if (Error_Code) printf("ERROR: BitMap2NodeName error %d\n", Error_Code);
    if (strcmp(Out_Line, "lx01,lx02,lx04") != 0) 
	printf("ERROR: BitMap2NodeName results bad %s vs %s\n", Out_Line, "lx01,lx02,lx04");
    free(Map1);
    free(Out_Line);

    Update_Time = (time_t)0;
    U_Map[0] = 0xdead;
    U_Map[1] = 0xbeef;
    Up_NodeBitMap = &U_Map[0];
    Idle_NodeBitMap = &U_Map[1];
    Error_Code = Validate_Node_Specs("lx01", 12, 345, 67);
    if (Error_Code) printf("ERROR: Validate_Node_Specs error1\n");
    Error_Code = Dump_Node(&Dump, &Dump_Size, &Update_Time);
    if (Error_Code) printf("ERROR: Dump_Node error %d\n", Error_Code);
    printf("NOTE: We expect Validate_Node_Specs to report bad CPU, RealMemory and TmpDisk on lx01\n");
    Error_Code = Validate_Node_Specs("lx01", 1, 2, 3);
    if (Error_Code != EINVAL) printf("ERROR: Validate_Node_Specs error2\n");

    Rehash();
    Dump_Hash();
    Node_Ptr   = Find_Node_Record("lx02");
    if (Node_Ptr == 0) 
	printf("ERROR: Find_Node_Record failure 1\n");
    else if (strcmp(Node_Ptr->Name, "lx02") != 0)
	printf("ERROR: Find_Node_Record failure 2\n");
    else if (Node_Ptr->LastResponse != (time_t)678) 
	printf("ERROR: Node default LastResponse not set\n");
    printf("NOTE: We expect Delete_Node_Record to report not finding a record for lx10\n");
    Error_Code = Delete_Node_Record("lx10");
    if (Error_Code != ENOENT) printf("ERROR: Delete_Node_Record failure 1\n");
    Error_Code = Delete_Node_Record("lx02");
    if (Error_Code != 0) printf("ERROR: Delete_Node_Record failure 2\n");
    printf("NOTE: We expect Find_Node_Record to report not finding a record for lx02\n");
    Node_Ptr   = Find_Node_Record("lx02");
    if (Node_Ptr != 0) printf("ERROR: Find_Node_Record failure 3\n");

    /* Check node name parsing */
    Out_Line = "linux[003-234]";
    Error_Code = Parse_Node_Name(Out_Line, &Format, &Start_Inx, &End_Inx, &Count_Inx);
    if (Error_Code != 0) 
	printf("ERROR: Parse_Node_Name error %d\n", Error_Code);
    else {
	if ((Start_Inx != 3) || (End_Inx != 234)) printf("ERROR: Parse_Node_Name failure\n");
	printf("Parse_Node_Name of \"%s\" produces format \"%s\", %d to %d, %d records\n", 
	    Out_Line, Format, Start_Inx, End_Inx, Count_Inx);
	if (Format) free(Format);
    } /* else */

#if PROTOTYPE_API
#if DEBUG_MODULE > 1
    printf("int:%d time_t:%d:\n", sizeof(int), sizeof(time_t));
    for (Start_Inx=0; Start_Inx<Dump_Size; Start_Inx++) {
	End_Inx = (int)Dump[Start_Inx];
	printf("%2.2x ",(unsigned)End_Inx);
	if (Start_Inx%20 == 19) printf("\n");
    }
#endif

    Error_Code = Load_Node(Dump, Dump_Size);
    if (Error_Code) printf("Load_Node error %d\n", Error_Code);

    Error_Code =  Load_Nodes_Up(&NodeBitMap, &BitMapSize);
    if (Error_Code) printf("Load_Nodes_Up error %d\n", Error_Code);
    if (BitMapSize > 0) printf("Load_Nodes_Up  BitMap[0]=0x%x, BitMapSize=%d\n", 
			NodeBitMap[0], BitMapSize);

    Error_Code =  Load_Nodes_Idle(&NodeBitMap, &BitMapSize);
    if (Error_Code) printf("Load_Nodes_Idle error %d\n", Error_Code);
    if (BitMapSize > 0) printf("Load_Nodes_Idle  BitMap[0]=0x%x, BitMapSize=%d\n", 
			NodeBitMap[0], BitMapSize);

    for (Start_Inx=0; ; Start_Inx++) {
	Error_Code = Load_Node_Config(Start_Inx, &CPUs, &RealMemory, &TmpDisk, &Weight, 
	    &Features, &Nodes, &NodeBitMap, &BitMapSize);
	if (Error_Code == ENOENT) break;
	if (Error_Code != 0)  {
	    printf("Load_Node_Config error %d\n", Error_Code);
	    break;
	} /* if */

	printf("Found config CPUs=%d, RealMemory=%d, TmpDisk=%d, ", 
	    CPUs, RealMemory, TmpDisk);
	printf("Weight=%d, Features=%s, Nodes=%s\n", Weight, Features, Nodes);
	if (BitMapSize > 0) 
	    printf("  BitMap[0]=0x%x, BitMapSize=%d\n", NodeBitMap[0], BitMapSize);
    } /* for */

    strcpy(Req_Name, "");	/* Start at beginning of partition list */
    while (1) {
	Error_Code = Load_Node_Name(Req_Name, Next_Name, &State, 
		&CPUs, &RealMemory, &TmpDisk, &Weight, &Features);
	if (Error_Code != 0)  {
	    printf("Load_Node_Name error %d\n", Error_Code);
	    break;
	} /* if */

	printf("Found node Name=%s, State=%d, CPUs=%d, RealMemory=%d, TmpDisk=%d, ", 
	    Req_Name, State, CPUs, RealMemory, TmpDisk);
	printf("Weight=%d, Features=%s\n", Weight, Features);

	if (strlen(Next_Name) == 0) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
#endif
    free(Dump);

    exit(0);
} /* main */
#endif


/*
 * BitMap2NodeName - Given a bitmap, build a node list representation
 * Input: BitMap - Bitmap pointer
 *        Node_List - Place to put node list
 * Output: Node_List - Set to node list or NULL on error 
 *         Returns 0 if no error, otherwise EINVAL or ENOMEM
 * NOTE: Consider returning the node list as a regular expression if helpful
 * NOTE: The caller must free memory at Node_List when no longer required
 */
int BitMap2NodeName(unsigned *BitMap, char **Node_List) {
    int Error_Code, Node_List_Size, i, empty;
    struct Node_Record *Node_Ptr;
    unsigned mask;

    Node_List[0] = NULL;
    Node_List_Size = 0;
    if (BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "NodeName2BitMap: BitMap is NULL\n");
#else
	syslog(LOG_ERR, "NodeName2BitMap: BitMap is NULL\n");
#endif
	return EINVAL;
    } /* if */

    Node_List[0] = malloc(BUF_SIZE);
    if (Node_List[0] == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "BitMap2NodeName: Can not allocate memory\n");
#else
	syslog(LOG_ALERT, "BitMap2NodeName: Can not allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    strcpy(Node_List[0], "");

    empty = 1;
    for (i=0; i<Node_Record_Count; i++) {
	if (BitMapValue(BitMap, i) == 0) continue;
	if (Node_List_Size < strlen(Node_List[0])+strlen((Node_Record_Table_Ptr+i)->Name)+1) {
	    Node_List_Size += BUF_SIZE;
	    Node_List[0] = realloc(Node_List[0], Node_List_Size);
	    if (Node_List[0] == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "BitMap2NodeName: Can not allocate memory\n");
#else
		syslog(LOG_ALERT, "BitMap2NodeName: Can not allocate memory\n");
#endif
		return ENOMEM;
	    } /* if */
	} /* if need more memory */
	if (empty == 0) strcat(Node_List[0], ","); 
	empty = 0;
	strcat(Node_List[0], (Node_Record_Table_Ptr+i)->Name);
    } /* for */
    Node_List[0] = realloc(Node_List[0], strlen(Node_List[0])+1);
    return 0;
} /* BitMap2NodeName */


/*
 * Create_Config_Record - Create a Config_Record entry, append it to the Config_List, 
 *	and set is values to the defaults.
 * Input: Error_Code - Pointer to an error code
 * Output: Returns pointer to the Config_Record
 *         Error_Code - set to zero if no error, errno otherwise
 * NOTE: The pointer returned is allocated memory that must be freed when no longer needed.
 */
struct Config_Record *Create_Config_Record(int *Error_Code) {
    struct Config_Record *Config_Point;

    Last_Node_Update = time(NULL);
    Config_Point = (struct Config_Record *)malloc(sizeof(struct Config_Record));
    if (Config_Point == (struct Config_Record *)NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n");
#endif
	*Error_Code = ENOMEM;
	return (struct Config_Record *)NULL;
    } /* if */

    /* Set default values */
    Config_Point->CPUs = Default_Config_Record.CPUs;
    Config_Point->RealMemory = Default_Config_Record.RealMemory;
    Config_Point->TmpDisk = Default_Config_Record.TmpDisk;
    Config_Point->Weight = Default_Config_Record.Weight;
    Config_Point->Nodes = NULL;
    Config_Point->NodeBitMap = NULL;
    if (Default_Config_Record.Feature) {
	Config_Point->Feature = (char *)malloc(strlen(Default_Config_Record.Feature)+1);
	if (Config_Point->Feature == (char *)NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n");
#endif
	    free(Config_Point);
	    *Error_Code = ENOMEM;
	    return (struct Config_Record *)NULL;
	} /* if */
	strcpy(Config_Point->Feature, Default_Config_Record.Feature);
    } else
	Config_Point->Feature = (char *)NULL;

    if (list_append(Config_List, Config_Point) == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Config_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Config_Record: unable to allocate memory\n");
#endif
	if (Config_Point->Feature) free(Config_Point->Feature);
	free(Config_Point);
	*Error_Code = ENOMEM;
	return (struct Config_Record *)NULL;
    } /* if */

    return Config_Point;
} /* Create_Config_Record */

/* 
 * Create_Node_Record - Create a node record
 * Input: Error_Code - Location to store error value in
 * Output: Error_Code - Set to zero if no error, errno otherwise
 *         Returns a pointer to the record or NULL if error
 * NOTE The record's values are initialized to those of Default_Record
 */
struct Node_Record *Create_Node_Record(int *Error_Code) {
    struct Node_Record *Node_Record_Point;
    int Old_Buffer_Size, New_Buffer_Size;

    *Error_Code = 0;
    Last_Node_Update = time(NULL);

    /* Round up the buffer size to reduce overhead of realloc */
    Old_Buffer_Size = (Node_Record_Count) * sizeof(struct Node_Record);
    Old_Buffer_Size = ((int)((Old_Buffer_Size / BUF_SIZE) + 1)) * BUF_SIZE;
    New_Buffer_Size = (Node_Record_Count+1) * sizeof(struct Node_Record);
    New_Buffer_Size = ((int)((New_Buffer_Size / BUF_SIZE) + 1)) * BUF_SIZE;
    if (Node_Record_Count == 0)
	Node_Record_Table_Ptr = (struct Node_Record *)malloc(New_Buffer_Size);
    else if (Old_Buffer_Size != New_Buffer_Size)
	Node_Record_Table_Ptr = (struct Node_Record *)realloc(Node_Record_Table_Ptr, New_Buffer_Size);

    if (Node_Record_Table_Ptr == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Create_Node_Record: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Create_Node_Record: unable to allocate memory\n");
#endif
	*Error_Code = ENOMEM;
	return;
    } /* if */

    Node_Record_Point = Node_Record_Table_Ptr + (Node_Record_Count++);
    strcpy(Node_Record_Point->Name,    Default_Node_Record.Name);
    Node_Record_Point->NodeState     = Default_Node_Record.NodeState;
    Node_Record_Point->LastResponse  = Default_Node_Record.LastResponse;
    return Node_Record_Point;
} /* Create_Node_Record */


/*
 * Delete_Config_Record - Delete all configuration records
 * Output: Returns 0 if no error, errno otherwise
 */
int Delete_Config_Record() {
    Last_Node_Update = time(NULL);
    (void)list_delete_all(Config_List, &List_Find_Config, "UNIVERSAL_KEY");
    return 0;
} /* Delete_Config_Record */


/* 
 * Delete_Node_Record - Delete record for node with specified name
 *   To avoid invalidating the bitmaps and hash table, we just clear the name 
 *   set its state to STATE_DOWN
 * Input: name - Name of the desired node, Delete all nodes if pointer is NULL
 * Output: return 0 on success, errno otherwise
 */
int Delete_Node_Record(char *name) {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */

    Last_Node_Update = time(NULL);
    Node_Record_Point = Find_Node_Record(name);
    if (Node_Record_Point == (struct Node_Record *)NULL) {
#if DEBUG_MODULE
	fprintf(stderr, "Delete_Node_Record: Attempt to delete non-existent node %s\n", name);
#else
	syslog(LOG_ALERT, "Delete_Node_Record: Attempt to delete non-existent node %s\n", name);
#endif
	return ENOENT;
    } /* if */

    strcpy(Node_Record_Point->Name, "");
    Node_Record_Point->NodeState = STATE_DOWN;
    return 0;
} /* Delete_Node_Record */


/* Print the Hash_Table contents, used for debugging or analysis of hash technique */
void Dump_Hash() {
    int i;

    if (Hash_Table ==  NULL) return;
    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+Hash_Table[i])->Name) == 0) continue;
	printf("Hash:%d:%s\n", i, (Node_Record_Table_Ptr+Hash_Table[i])->Name);
    } /* for */
} /* Dump_Hash */


/* 
 * Dump_Node - Dump all configuration and node information to a buffer
 * Input: Buffer_Ptr - Location into which a pointer to the data is to be stored.
 *                     The data buffer is actually allocated by Dump_Node and the 
 *                     calling function must free the storage.
 *         Buffer_Size - Location into which the size of the created buffer is in bytes
 *         Update_Time - Dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * Output: Buffer_Ptr - The pointer is set to the allocated buffer.
 *         Buffer_Size - Set to size of the buffer in bytes
 *         Update_Time - set to time partition records last updated
 *         Returns 0 if no error, errno otherwise
 * NOTE: In this prototype, the buffer at *Buffer_Ptr must be freed by the caller
 * NOTE: This is a prototype for a function to ship data partition to an API.
 */
int Dump_Node(char **Buffer_Ptr, int *Buffer_Size, time_t *Update_Time) {
    ListIterator Config_Record_Iterator;	/* For iterating through Config_List */
    struct Config_Record *Config_Record_Point;	/* Pointer to Config_Record */
    char *Buffer;
    int Buffer_Offset, Buffer_Allocated, i, inx, Record_Size;
    struct Config_Specs {
	struct Config_Record *Config_Record_Point;
    };
    struct Config_Specs *Config_Spec_List = NULL;
    int Config_Spec_List_Cnt = 0;

    Buffer_Ptr[0] = NULL;
    *Buffer_Size = 0;
    if (*Update_Time == Last_Node_Update) return 0;

    Config_Record_Iterator = list_iterator_create(Config_List);
    if (Config_Record_Iterator == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node: list_iterator_create unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Node: list_iterator_create unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    Buffer_Allocated = BUF_SIZE + (Node_Record_Count*2);
    Buffer = malloc(Buffer_Allocated);
    if (Buffer == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
	list_iterator_destroy(Config_Record_Iterator);
	return ENOMEM;
    } /* if */

    /* Write haeader, version and time */
    Buffer_Offset = 0;
    i = CONFIG_STRUCT_VERSION;
    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
    Buffer_Offset += sizeof(i);
    memcpy(Buffer+Buffer_Offset, &Last_Node_Update, sizeof(Last_Node_Update));
    Buffer_Offset += sizeof(Last_Part_Update);

    /* Write up and idle node bitmaps */
    if ((Node_Record_Count > 0) && Up_NodeBitMap){
	i = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
	i *= sizeof(unsigned);
	memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	Buffer_Offset += sizeof(i);
	memcpy(Buffer+Buffer_Offset, Up_NodeBitMap, i); 
	Buffer_Offset += i;
    } else {
	i = 0;
	memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	Buffer_Offset += sizeof(i);
    } /* else */
    if ((Node_Record_Count > 0) && Idle_NodeBitMap){
	i = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
	i *= sizeof(unsigned);
	memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	Buffer_Offset += sizeof(i);
	memcpy(Buffer+Buffer_Offset, Idle_NodeBitMap, i); 
	Buffer_Offset += i;
    } else {
	i = 0;
	memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	Buffer_Offset += sizeof(i);
    } /* else */

    /* Write configuration records */
    while (Config_Record_Point = (struct Config_Record *)list_next(Config_Record_Iterator)) {
	Record_Size = (7 * sizeof(int)) + ((Node_Record_Count + (sizeof(unsigned)*8) - 1) / 8);
	if (Config_Record_Point->Feature) Record_Size+=strlen(Config_Record_Point->Feature)+1;
	if (Config_Record_Point->Nodes) Record_Size+=strlen(Config_Record_Point->Nodes)+1;

	if ((Buffer_Offset+Record_Size) >= Buffer_Allocated) { /* Need larger buffer */
	    Buffer_Allocated += (Record_Size + BUF_SIZE);
	    Buffer = realloc(Buffer, Buffer_Allocated);
	    if (Buffer == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
		if (Config_Spec_List) free(Config_Spec_List);
		list_iterator_destroy(Config_Record_Iterator);
		return ENOMEM;
	    } /* if */
	} /* if */

	if (Config_Spec_List_Cnt == 0) 
	    Config_Spec_List = malloc(sizeof(struct Config_Specs));
	else
	    Config_Spec_List = realloc(Config_Spec_List, 
		(Config_Spec_List_Cnt+1)*sizeof(struct Config_Specs));
	if (Config_Spec_List == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
	    free(Buffer);
	    list_iterator_destroy(Config_Record_Iterator);
	    free(Config_Spec_List);
	    return ENOMEM;
	} /* if */
	Config_Spec_List[Config_Spec_List_Cnt++].Config_Record_Point = Config_Record_Point;

	memcpy(Buffer+Buffer_Offset, &Config_Record_Point->CPUs, sizeof(Config_Record_Point->CPUs)); 
	Buffer_Offset += sizeof(Config_Record_Point->CPUs);

	memcpy(Buffer+Buffer_Offset, &Config_Record_Point->RealMemory, 
		sizeof(Config_Record_Point->RealMemory)); 
	Buffer_Offset += sizeof(Config_Record_Point->RealMemory);

	memcpy(Buffer+Buffer_Offset, &Config_Record_Point->TmpDisk, sizeof(Config_Record_Point->TmpDisk)); 
	Buffer_Offset += sizeof(Config_Record_Point->TmpDisk);

	memcpy(Buffer+Buffer_Offset, &Config_Record_Point->Weight, sizeof(Config_Record_Point->Weight)); 
	Buffer_Offset += sizeof(Config_Record_Point->Weight);

	if (Config_Record_Point->Feature) {
	    i = strlen(Config_Record_Point->Feature) + 1;
	    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	    Buffer_Offset += sizeof(i);
	    memcpy(Buffer+Buffer_Offset, Config_Record_Point->Feature, i); 
	    Buffer_Offset += i;
	} else {
	    i = 0;
	    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	    Buffer_Offset += sizeof(i);
	} /* else */

	if (Config_Record_Point->Nodes) {
	    i = strlen(Config_Record_Point->Nodes) + 1;
	    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	    Buffer_Offset += sizeof(i);
	    memcpy(Buffer+Buffer_Offset, Config_Record_Point->Nodes, i); 
	    Buffer_Offset += i;
	} else {
	    i = 0;
	    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	    Buffer_Offset += sizeof(i);
	} /* else */

	if ((Node_Record_Count > 0) && (Config_Record_Point->NodeBitMap)){
	    i = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / (sizeof(unsigned)*8);
	    i *= sizeof(unsigned);
	    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	    Buffer_Offset += sizeof(i);
	    memcpy(Buffer+Buffer_Offset, Config_Record_Point->NodeBitMap, i); 
	    Buffer_Offset += i;
	} else {
	    i = 0;
	    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	    Buffer_Offset += sizeof(i);
	} /* else */

    } /* while */
    list_iterator_destroy(Config_Record_Iterator);

    /* Mark end of configuration data , looks like CPUs = -1 */
    i = -1;
    memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
    Buffer_Offset += sizeof(i);


    /* Write node records */
    for (inx=0; inx<Node_Record_Count; inx++) {
	if (strlen((Node_Record_Table_Ptr+inx)->Name) == 0) continue;
	Record_Size = MAX_NAME_LEN + 2 * sizeof(int);
	if ((Buffer_Offset+Record_Size) >= Buffer_Allocated) { /* Need larger buffer */
	    Buffer_Allocated += (Record_Size + BUF_SIZE);
	    Buffer = realloc(Buffer, Buffer_Allocated);
	    if (Buffer == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
		free(Config_Spec_List);
		return ENOMEM;
	    } /* if */
	} /* if */

	memcpy(Buffer+Buffer_Offset, (Node_Record_Table_Ptr+inx)->Name, 
		sizeof((Node_Record_Table_Ptr+inx)->Name)); 
	Buffer_Offset += sizeof((Node_Record_Table_Ptr+inx)->Name);

	i = (int)(Node_Record_Table_Ptr+inx)->NodeState;
	memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	Buffer_Offset += sizeof(i);
	for (i=0; i<Config_Spec_List_Cnt; i++) {
	    if (Config_Spec_List[i].Config_Record_Point ==
		(Node_Record_Table_Ptr+inx)->Config_Ptr) break;
	} /* for (i */
	if (i < Config_Spec_List_Cnt) 
	    i++;
	else
	    i = 0;
	memcpy(Buffer+Buffer_Offset, &i, sizeof(i)); 
	Buffer_Offset += sizeof(i);

    } /* for (inx */
    free(Config_Spec_List);

    Buffer = realloc(Buffer, Buffer_Offset);
    if (Buffer == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Node: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Node: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    Buffer_Ptr[0] = Buffer;
    *Buffer_Size = Buffer_Offset;
    *Update_Time = Last_Node_Update;
    return 0;
} /* Dump_Node */


/* 
 * Find_Node_Record - Find a record for node with specified name,
 * Input: name - name of the desired node 
 * Output: return pointer to node record or NULL if not found
 */
struct Node_Record *Find_Node_Record(char *name) {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i;

    /* Try to find in hash table first */
    if (Hash_Table) {
	i = Hash_Index(name);
        if (strcmp((Node_Record_Table_Ptr+Hash_Table[i])->Name, name) == 0) 
		return (Node_Record_Table_Ptr+Hash_Table[i]);
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Node_Record: Hash table lookup failure for %s\n", name);
	Dump_Hash();
#else
	syslog(LOG_DEBUG, "Find_Node_Record: Hash table lookup failure for %s\n", name);
#endif
    } /* if */

    /* Revert to sequential search */
    for (i=0; i<Node_Record_Count; i++) {
	if (strcmp(name, (Node_Record_Table_Ptr+i)->Name) != 0) continue;
	return (Node_Record_Table_Ptr+i);
    } /* for */

    if (Hash_Table) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Find_Node_Record: Lookup failure for %s\n", name);
#else
	syslog(LOG_ERR, "Find_Node_Record: Lookup failure for %s\n", name);
#endif
    } /* if */
    return (struct Node_Record *)NULL;
} /* Find_Node_Record */


/* 
 * Hash_Index - Return a hash table index for the given node name 
 * This code is optimized for names containing a base-ten suffix (e.g. "lx04")
 * Input: The node's name
 * Output: Return code is the hash table index
 */
int Hash_Index(char *name) {
    int i, inx, tmp;

    if (Node_Record_Count == 0) return 0;		/* Degenerate case */
    inx = 0;

#if ( HASH_BASE == 10 )
    for (i=0; ;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;			/* end if string */
	if ((tmp >= (int)'0') && (tmp <= (int)'9')) 
	    inx = (inx * HASH_BASE) + (tmp - (int)'0');
    } /* for */
#elif ( HASH_BASE == 8 )
    for (i=0; ;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;			/* end if string */
	if ((tmp >= (int)'0') && (tmp <= (int)'7')) 
	    inx = (inx * HASH_BASE) + (tmp - (int)'0');
    } /* for */

#else
    for (i=0; i<5;i++) { 
	tmp = (int) name[i];
	if (tmp == 0) break;					/* end if string */
	if ((tmp >= (int)'0') && (tmp <= (int)'9')) {		/* value 0-9 */
	    tmp -= (int)'0';
	} else if ((tmp >= (int)'a') && (tmp <= (int)'z')) {	/* value 10-35 */
	    tmp -= (int)'a';
	    tmp += 10;
	} else if ((tmp >= (int)'A') && (tmp <= (int)'Z')) {	/* value 10-35 */
	    tmp -= (int)'A';
	    tmp += 10;
	} else {
	    tmp = 36;
	}
	inx = (inx * 37) + tmp;
    } /* for */
 #endif

    inx = inx % Node_Record_Count;
    return inx;
} /* Hash_Index */


/* 
 * Init_Node_Conf - Initialize the node configuration values. 
 * This should be called before creating any node or configuration entries.
 * Output: return value - 0 if no error, otherwise an error code
 */
int Init_Node_Conf() {
    Last_Node_Update = time(NULL);

    Node_Record_Count = 0;
    if (Node_Record_Table_Ptr) {
	free(Node_Record_Table_Ptr);
	Node_Record_Table_Ptr = NULL;
    }
    if (Hash_Table)  {
	free(Hash_Table);
	Hash_Table = NULL;
    }

    strcpy(Default_Node_Record.Name, "DEFAULT");
    Default_Node_Record.NodeState    = STATE_UNKNOWN;
    Default_Node_Record.LastResponse = (time_t)0;
    Default_Config_Record.CPUs       = 1;
    Default_Config_Record.RealMemory = 1;
    Default_Config_Record.TmpDisk    = 1;
    Default_Config_Record.Weight     = 1;
    if (Default_Config_Record.Feature) free(Default_Config_Record.Feature);
    Default_Config_Record.Feature    = (char *)NULL;
    if (Default_Config_Record.Nodes) free(Default_Config_Record.Nodes);
    Default_Config_Record.Nodes      = (char *)NULL;
    if (Default_Config_Record.NodeBitMap) free(Default_Config_Record.NodeBitMap);
    Default_Config_Record.NodeBitMap = (unsigned *)NULL;

    if (Config_List) 	/* Delete defunct configuration entries */
	(void)Delete_Config_Record();
    else
	Config_List = list_create(&List_Delete_Config);

    if (Config_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_Node_Conf: list_create can not allocate memory\n");
#else
	syslog(LOG_ALERT, "Init_Node_Conf: list_create can not allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    return 0;
} /* Init_Node_Conf */


/* List_Compare_Config - Compare two entry from the config list based upon weight, 
 * see list.h for documentation */
int List_Compare_Config(void *Config_Entry1, void *Config_Entry2) {
    int Weight1, Weight2;
    Weight1 = ((struct Config_Record *)Config_Entry1)->Weight;
    Weight2 = ((struct Config_Record *)Config_Entry2)->Weight;
    return (Weight1 - Weight2);
} /* List_Compare_Config */


/* List_Delete_Config - Delete an entry from the config list, see list.h for documentation */
void List_Delete_Config(void *Config_Entry) {
    struct Config_Record *Config_Record_Point;	/* Pointer to Config_Record */
    Config_Record_Point = (struct Config_Record *)Config_Entry;
    if (Config_Record_Point->Feature)     free(Config_Record_Point->Feature);
    if (Config_Record_Point->Nodes)       free(Config_Record_Point->Nodes);
    if (Config_Record_Point->NodeBitMap)  free(Config_Record_Point->NodeBitMap);
    free(Config_Record_Point);
} /* List_Delete_Config */


/* List_Find_Config - Find an entry in the config list, see list.h for documentation 
 * Key is partition name or "UNIVERSAL_KEY" for all config */
int List_Find_Config(void *Config_Entry, void *key) {
    if (strcmp(key, "UNIVERSAL_KEY") == 0) return 1;
    return 0;
} /* List_Find_Config */


/*
 * NodeName2BitMap - Given a node list, build a bitmap representation
 * Input: Node_List - List of nodes
 *        BitMap - Place to put bitmap pointer
 * Output: BitMap - Set to bitmap or NULL on error 
 *         Returns 0 if no error, otherwise EINVAL or ENOMEM
 * NOTE: The caller must free memory at BitMap when no longer required
 */
int NodeName2BitMap(char *Node_List, unsigned **BitMap) {
    int Error_Code, i, size;
    int Start_Inx, End_Inx, Count_Inx;
    char *Format, This_Node_Name[BUF_SIZE], *My_Node_List, *str_ptr1, *str_ptr2;
    struct Node_Record *Node_Record_Point;
    unsigned *My_BitMap;

    BitMap[0] = NULL;
    if (Node_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "NodeName2BitMap: Node_List is NULL\n");
#else
	syslog(LOG_ERR, "NodeName2BitMap: Node_List is NULL\n");
#endif
	return EINVAL;
    } /* if */
    if (Node_Record_Count == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "NodeName2BitMap: System has no nodes\n");
#else
	syslog(LOG_ERR, "NodeName2BitMap: System has no nodes\n");
#endif
	return EINVAL;
    } /* if */

    My_Node_List = malloc(strlen(Node_List)+1);
    if (My_Node_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "NodeName2BitMap: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "NodeName2BitMap: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    strcpy(My_Node_List, Node_List);

    size = (Node_Record_Count + (sizeof(unsigned)*8) - 1) / 
		(sizeof(unsigned)*8); 	/* Unsigned int records in bitmap */
    size *= 8;				/* Bytes in bitmap */
    My_BitMap = (unsigned *)malloc(size);
    if (My_BitMap == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "NodeName2BitMap: unable to allocate My_BitMap memory\n");
#else
	syslog(LOG_ALERT, "NodeName2BitMap: unable to allocate memory\n");
#endif
	free(My_Node_List);
	return ENOMEM;
    } /* if */
    memset(My_BitMap, 0, size);

    str_ptr2 = (char *)strtok_r(My_Node_List, ",", &str_ptr1);
    while (str_ptr2) {	/* Break apart by comma separators */
	Error_Code = Parse_Node_Name(str_ptr2, &Format, &Start_Inx, &End_Inx, &Count_Inx);
	if (Error_Code) {
	    free(My_Node_List);
	    free(My_BitMap);
	    return EINVAL;
	} /* if */
	if (strlen(Format) >= sizeof(This_Node_Name)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "NodeName2BitMap: Node name specification too long: %s\n", Format);
#else
	    syslog(LOG_ERR, "NodeName2BitMap: Node name specification too long: %s\n", Format);
#endif
	    free(My_Node_List);
	    free(My_BitMap);
	    free(Format);
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
		fprintf(stderr, "NodeName2BitMap: Invalid node specified %s\n", This_Node_Name);
#else
		syslog(LOG_ERR, "NodeName2BitMap: Invalid node specified %s\n", This_Node_Name);
#endif
		free(My_Node_List);
		free(My_BitMap);
		free(Format);
		return EINVAL;
	    } /* if */
	    BitMapSet(My_BitMap, (int)(Node_Record_Point - Node_Record_Table_Ptr));
	} /* for */
	str_ptr2 = (char *)strtok_r(NULL, ",", &str_ptr1);
    } /* while */

    free(My_Node_List);
    free(Format);
    BitMap[0] =My_BitMap;
    return 0;
} /* NodeName2BitMap */


/* 
 * Parse_Node_Name - Parse the node name for regular expressions and return a sprintf format 
 * generate multiple node names as needed.
 * Input: NodeName - Node name to parse
 * Output: Format - sprintf format for generating names
 *         Start_Inx - First index to used
 *         End_Inx - Last index value to use
 *         Count_Inx - Number of index values to use (will be zero if none)
 *         return 0 if no error, error code otherwise
 * NOTE: The calling program must execute free(Format) when the storage location is no longer needed
 */
int Parse_Node_Name(char *NodeName, char **Format, int *Start_Inx, int *End_Inx, int *Count_Inx) {
    int Base, Format_Pos, Precision, i;
    char Type[1];

    i = strlen(NodeName);
    Format[0] = (char *)malloc(i+1);
    if (Format[0] == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Parse_Node_Name: unable to allocate memory\n");
#else
	syslog(LOG_ERR, "Parse_Node_Name: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */

    *Start_Inx = 0;
    *End_Inx   = 0;
    *Count_Inx = 0;
    Format_Pos = 0;
    Base = 0;
    Format[0][Format_Pos] = (char)NULL;
    i = 0;
    while (1) {
	if (NodeName[i] == (char)NULL) break;
	if (NodeName[i] == '\\') {
	    if (NodeName[++i] == (char)NULL) break;
	    Format[0][Format_Pos++] = NodeName[i++];
	} else if (NodeName[i] == '[') {		/* '[' preceeding number range */
	    if (NodeName[++i] == (char)NULL) break;
	    if (Base != 0) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '[' in node name %s\n", NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '[' in node name %s\n", NodeName);
#endif
		free(Format[0]);
		return EINVAL;
	    } /* if */
	    if (NodeName[i] == 'o') {
		Type[0] = NodeName[i++];
		Base = 8;
	    } else {
		Type[0] = 'd';
		Base = 10;
	    } /* else */
	    Precision = 0;
	    while (1) {
		if ((NodeName[i] >= '0') && (NodeName[i] <= '9')) {
		    *Start_Inx = ((*Start_Inx) * Base) + (int)(NodeName[i++] - '0');
		    Precision++;
		    continue;
		} /* if */
		if (NodeName[i] == '-') {		/* '-' between numbers */
		    i++;
		    break;
		} /* if */
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#endif
		free(Format[0]);
		return EINVAL;
	    } /* while */
	    while (1) {
		if ((NodeName[i] >= '0') && (NodeName[i] <= '9')) {
		    *End_Inx = ((*End_Inx) * Base) + (int)(NodeName[i++] - '0');
		    continue;
		} /* if */
		if (NodeName[i] == ']') {		/* ']' terminating number range */ 
		    i++;
		    break;
		} /* if */
#if DEBUG_SYSTEM
		fprintf(stderr, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#else
		syslog(LOG_ALERT, "Parse_Node_Name: Invalid '%c' in node name %s\n", 
			NodeName[i], NodeName);
#endif
		free(Format[0]);
		return EINVAL;
	    } /* while */
	    *Count_Inx = (*End_Inx - *Start_Inx) + 1;
	    Format[0][Format_Pos++] = '%';
	    Format[0][Format_Pos++] = '.';
	    if (Precision > 9) Format[0][Format_Pos++] = '0' + (Precision/10);
	    Format[0][Format_Pos++] = '0' + (Precision%10);
	    Format[0][Format_Pos++] = Type[0];
	} else {
	    Format[0][Format_Pos++] = NodeName[i++];
	} /* else */
    } /* while */
    Format[0][Format_Pos] = (char)NULL;
    return 0;
} /* Parse_Node_Name */


/* 
 * Rehash - Build a hash table of the Node_Record entries. This is a large hash table 
 * to permit the immediate finding of a record based only upon its name without regards 
 * to the number. There should be no need for a search. The algorithm is optimized for 
 * node names with a base-ten sequence number suffix. If you have a large cluster and 
 * use a different naming convention, this function and/or the Hash_Index function 
 * should be re-written.
 */
void Rehash() {
    struct Node_Record *Node_Record_Point;	/* Pointer to Node_Record */
    int i, inx;

    Hash_Table = (int *)realloc(Hash_Table, (sizeof(int) * Node_Record_Count));

    if (Hash_Table == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Rehash: list_append can not allocate memory\n");
#else
	syslog(LOG_ALERT, "Rehash: list_append can not allocate memory\n");
#endif
	return;
    } /* if */
    memset(Hash_Table, 0, (sizeof(int) * Node_Record_Count));

    for (i=0; i<Node_Record_Count; i++) {
	if (strlen((Node_Record_Table_Ptr+i)->Name) == 0) continue;
	inx = Hash_Index((Node_Record_Table_Ptr+i)->Name);
	Hash_Table[inx] = i;
    } /* for */

} /* Rehash */


/* 
 * Update_Node - Update a node's configuration data
 * Input: NodeName - Node's name
 *        Spec - The updates to the node's specification 
 * Output:  Return - 0 if no error, otherwise an error code
 * NOTE: The contents of Spec are overwritten by white space
 */
int Update_Node(char *NodeName, char *Spec) {
    int Bad_Index, Error_Code, i;
    char *Format, *State, This_Node_Name[BUF_SIZE];
    int Start_Inx, End_Inx, Count_Inx, State_Val;
    char *str_ptr1, *str_ptr2, *My_Node_List;
    struct Node_Record *Node_Record_Point;

    if (strcmp(NodeName, "DEFAULT") == 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Node: Invalid node name %s\n", NodeName);
#else
	syslog(LOG_ALERT, "Update_Node: Invalid node name  %s\n", NodeName);
#endif
	return EINVAL;
    } /* if */

    State_Val = NO_VAL;
    State = NULL;
    if (Error_Code=Load_String (&State, "State=", Spec)) return Error_Code;
    if (State != NULL) {
	for (i=0; i<=STATE_END; i++) {
	    if (strcmp(Node_State_String[i], "END") == 0) break;
	    if (strcmp(Node_State_String[i], State) == 0) {
		State_Val = i;
		break;
	    } /* if */
	} /* for */
	if (State_Val == NO_VAL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Update_Node: Invalid State %s for NodeName %s\n", State, NodeName);
#else
	    syslog(LOG_ERR, "Update_Node: Invalid State %s for NodeName %s\n", State, NodeName);
#endif
	    free(State);
	    return EINVAL;
	} /* if */
	free(State);
    } /* if */

    /* Check for anything else (unparsed) in the specification */
    Bad_Index = -1;
    for (i=0; i<strlen(Spec); i++) {
	if (Spec[i] == '\n') Spec[i]=' ';
	if (isspace((int)Spec[i])) continue;
	Bad_Index=i;
	break;
    } /* if */

    if (Bad_Index != -1) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Node: Ignored node %s update specification: %s\n", 
		NodeName, &Spec[Bad_Index]);
#else
	syslog(LOG_ERR, "Update_Node: Ignored node %s update specification: %s\n", 
		NodeName, &Spec[Bad_Index]);
#endif
	return EINVAL;
    } /* if */

    My_Node_List = malloc(strlen(NodeName)+1);
    if (My_Node_List == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Update_Node: Unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Update_Node: Unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
    strcpy(My_Node_List, NodeName);
    str_ptr2 = (char *)strtok_r(My_Node_List, ",", &str_ptr1);
    while (str_ptr2) {	/* Break apart by comma separators */
	Error_Code = Parse_Node_Name(str_ptr2, &Format, &Start_Inx, &End_Inx, &Count_Inx);
	if (Error_Code) return Error_Code;
	if (strlen(Format) >= sizeof(This_Node_Name)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Update_Node: Node name specification too long: %s\n", Format);
#else
	    syslog(LOG_ERR, "Update_Node: Node name specification too long: %s\n", Format);
#endif
	    free(Format);
	    Error_Code = EINVAL;
	    break;
	} /* if */
	for (i=Start_Inx; i<=End_Inx; i++) {
	    if (Count_Inx == 0) 
		strncpy(This_Node_Name, Format, sizeof(This_Node_Name));
	    else
		sprintf(This_Node_Name, Format, i);
	    Node_Record_Point = Find_Node_Record(This_Node_Name);
	    if (Node_Record_Point == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Update_Node: Node name %s does not exist, can not be updated\n", This_Node_Name);
#else
		syslog(LOG_ERR, "Update_Node: Node name %s does not exist, can not be updated\n", This_Node_Name);
#endif
		Error_Code = EINVAL;
		break;
	    } /* if */
	    if (State_Val != NO_VAL) {
		Node_Record_Point->NodeState = State_Val;
#if DEBUG_SYSTEM
		fprintf(stderr, "Update_Node: Node %s state set to %s\n", 
			This_Node_Name, Node_State_String[State_Val]);
#else
		syslog(LOG_INFO, "Update_Node: Node %s state set to %s\n", 
			This_Node_Name, Node_State_String[State_Val]);
#endif
	    } /* if */
	} /* for */
	free(Format);
	str_ptr2 = (char *)strtok_r(NULL, ",", &str_ptr1);
    } /* while */

    free(My_Node_List);
    return Error_Code;
} /* Update_Node */


/*
 * Validate_Node_Specs - Validate the node's specifications as valid, 
 *   if not set state to DOWN, in any case update LastResponse
 * Input: NodeName - Name of the node
 *        CPUs - Number of CPUs measured
 *        RealMemory - MegaBytes of RealMemory measured
 *        TmpDisk - MegaBytes of TmpDisk measured
 * Output: Returns 0 if no error, ENOENT if no such node, EINVAL if values too low
 */ 
int Validate_Node_Specs(char *NodeName, int CPUs, int RealMemory, int TmpDisk) {
    int Error_Code;
    struct Config_Record *Config_Ptr;
    struct Node_Record *Node_Ptr;

    Node_Ptr = Find_Node_Record(NodeName);
    if (Node_Ptr == NULL) return ENOENT;
    Node_Ptr->LastResponse = time(NULL);

    Config_Ptr = Node_Ptr->Config_Ptr;
    Error_Code = 0;

    if (CPUs < Config_Ptr->CPUs) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Validate_Node_Specs: Node %s has low CPU count\n", NodeName);
#else
	syslog(LOG_ERR, "Validate_Node_Specs: Node %s has low CPU count\n", NodeName);
#endif
	Error_Code = EINVAL;
    } /* if */

    if (RealMemory < Config_Ptr->RealMemory) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Validate_Node_Specs: Node %s has low RealMemory size\n", NodeName);
#else
	syslog(LOG_ERR, "Validate_Node_Specs: Node %s has low RealMemory size\n", NodeName);
#endif
	Error_Code = EINVAL;
    } /* if */

    if (TmpDisk < Config_Ptr->TmpDisk) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Validate_Node_Specs: Node %s has low TmpDisk size\n", NodeName);
#else
	syslog(LOG_ERR, "Validate_Node_Specs: Node %s has low TmpDisk size\n", NodeName);
#endif
	Error_Code = EINVAL;
    } /* if */

    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Validate_Node_Specs: Setting node %s state to DOWN\n", NodeName);
#else
	syslog(LOG_ERR, "Validate_Node_Specs: Setting node %s state to DOWN\n", NodeName);
#endif
	Node_Ptr->NodeState = STATE_DOWN;
	BitMapClear(Up_NodeBitMap, (Node_Ptr - Node_Record_Table_Ptr));

    } else if ((Node_Ptr->NodeState == STATE_DOWN) || 
               (Node_Ptr->NodeState == STATE_UNKNOWN)) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Validate_Node_Specs: Setting node %s state to UP\n", NodeName);
#else
	syslog(LOG_ERR, "Validate_Node_Specs: Setting node %s state to UP\n", NodeName);
#endif
	Node_Ptr->NodeState = STATE_UP;
	BitMapSet(Up_NodeBitMap, (Node_Ptr - Node_Record_Table_Ptr));
    } /* else */

    return Error_Code;
}  /* Validate_Node_Specs */


#if PROTOTYPE_API
/*
 * Load_Node - Load the supplied node information buffer for use by info gathering APIs
 * Input: Buffer - Pointer to node information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid
 */
int Load_Node(char *Buffer, int Buffer_Size) {
    int Version;

    if (Buffer_Size < (4*sizeof(int))) return EINVAL;	/* Too small to be legitimate */

    memcpy(&Version, Buffer, sizeof(Version));
    if (Version != CONFIG_STRUCT_VERSION) return EINVAL;	/* Incompatable versions */

    Node_API_Buffer = Buffer;
    Node_API_Buffer_Size = Buffer_Size;
    return 0;
} /* Load_Node */


/* 
 * Load_Node_Config - Load the state information about configuration at specified inxed
 * Input: Index - zero origin index of the configuration requested
 *        CPUs, etc. - Pointers into which the information is to be stored
 * Output: CPUs, etc. - The node's state information
 *         BitMap_Size - Size of BitMap in bytes
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 */
int Load_Node_Config(int Index, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features, 
	char **Nodes, unsigned **NodeBitMap, int *BitMap_Size) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;
    struct Config_Record Read_Config_List;
    int Read_Config_List_Cnt = 0;
    struct Node_Record My_Node_Entry;
    int My_BitMap_Size;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += (sizeof(i) + i);
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += (sizeof(i) + i);

    /* Load the configuration records */
    while ((Buffer_Loc+(sizeof(int)*7)) <= 
	   (Node_API_Buffer+Node_API_Buffer_Size)) {	

	/* Load all info for next configuration */
	memcpy(&Read_Config_List.CPUs, Buffer_Loc, 
		sizeof(Read_Config_List.CPUs)); 
	Buffer_Loc += sizeof(Read_Config_List.CPUs);
	if (Read_Config_List.CPUs == -1) break; /* End of config recs */

	memcpy(&Read_Config_List.RealMemory, Buffer_Loc, 
		sizeof(Read_Config_List.RealMemory)); 
	Buffer_Loc += sizeof(Read_Config_List.RealMemory);

	memcpy(&Read_Config_List.TmpDisk, Buffer_Loc, 
		sizeof(Read_Config_List.TmpDisk)); 
	Buffer_Loc += sizeof(Read_Config_List.TmpDisk);

	memcpy(&Read_Config_List.Weight, Buffer_Loc, 
		sizeof(Read_Config_List.Weight)); 
	Buffer_Loc += sizeof(Read_Config_List.Weight);

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Config: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Config: malformed buffer\n");
#endif
	    return EINVAL;
	} /* if */
	if (i)
	    Read_Config_List.Feature = Buffer_Loc;
	else
	    Read_Config_List.Feature = NULL;
	Buffer_Loc += i;

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Config: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Config: malformed buffer\n");
#endif
	    return EINVAL;
	} /* if */
	if (i)
	    Read_Config_List.Nodes = Buffer_Loc;
	else
	    Read_Config_List.Nodes = NULL;
	Buffer_Loc += i;

	memcpy(&My_BitMap_Size, Buffer_Loc, sizeof(My_BitMap_Size)); 
	Buffer_Loc += sizeof(My_BitMap_Size);
	if ((Buffer_Loc+My_BitMap_Size) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Config: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Config: malformed buffer\n");
#endif
	    return EINVAL;
	} /* if */
	if (My_BitMap_Size)
	    Read_Config_List.NodeBitMap = (unsigned *)Buffer_Loc;
	else
	    Read_Config_List.NodeBitMap = NULL;
	Buffer_Loc += My_BitMap_Size;

	if (Read_Config_List_Cnt++ != Index) continue;

	*CPUs 		= Read_Config_List.CPUs;
	*RealMemory	= Read_Config_List.RealMemory;
	*TmpDisk	= Read_Config_List.TmpDisk;
	*Weight		= Read_Config_List.Weight;
	Features[0]	= Read_Config_List.Feature;
	Nodes[0]	= Read_Config_List.Nodes;
	NodeBitMap[0]	= Read_Config_List.NodeBitMap;
	*BitMap_Size	= My_BitMap_Size;
	return 0;
    } /* while */

    *CPUs 		= 0;
    *RealMemory		= 0;
    *TmpDisk		= 0;
    *Weight		= 0;
    Features[0]		= NULL;
    NodeBitMap[0]	= NULL;
    *BitMap_Size	= 0;
    return ENOENT;
} /* Load_Node_Config */


/* 
 * Load_Nodes_Idle - Load the bitmap of idle nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
int Load_Nodes_Idle(unsigned **NodeBitMap, int *BitMap_Size) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i) + i;
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i);

    if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) return EINVAL;	
    NodeBitMap[0] = (unsigned *)Buffer_Loc;
    *BitMap_Size = i;
    return 0;
} /* Load_Nodes_Idle */


/* 
 * Load_Node_Name - Load the state information about the named node
 * Input: Req_Name - Name of the node for which information is requested
 *		     if "", then get info for the first node in list
 *        Next_Name - Location into which the name of the next node is 
 *                   stored, "" if no more
 *        State, etc. - Pointers into which the information is to be stored
 * Output: Req_Name - The node's name is stored here
 *         Next_Name - The name of the next node in the list is stored here
 *         State, etc. - The node's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 */
int Load_Node_Name(char *Req_Name, char *Next_Name, int *State, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;
    struct Config_Record *Read_Config_List = NULL;
    int Read_Config_List_Cnt = 0;
    struct Node_Record My_Node_Entry;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i) + i;
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i) + i;

    /* Load the configuration records */
    while ((Buffer_Loc+(sizeof(int)*7)) <= 
	   (Node_API_Buffer+Node_API_Buffer_Size)) {	
	if (Read_Config_List_Cnt)
	    Read_Config_List = realloc(Read_Config_List, 
		sizeof(struct Config_Record) * (Read_Config_List_Cnt+1));
	else
	    Read_Config_List = malloc(sizeof(struct Config_Record));
	if (Read_Config_List == NULL) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: unable to allocate memory\n");
#else
	    syslog(LOG_ALERT, "Load_Node_Name: unable to allocate memory\n");
#endif
	    return ENOMEM;
	} /* if */

	/* Load all info for next configuration */
	memcpy(&Read_Config_List[Read_Config_List_Cnt].CPUs, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].CPUs)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].CPUs);
	if (Read_Config_List[Read_Config_List_Cnt].CPUs == -1) break; /* End of config recs */

	memcpy(&Read_Config_List[Read_Config_List_Cnt].RealMemory, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].RealMemory)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].RealMemory);

	memcpy(&Read_Config_List[Read_Config_List_Cnt].TmpDisk, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].TmpDisk)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].TmpDisk);

	memcpy(&Read_Config_List[Read_Config_List_Cnt].Weight, Buffer_Loc, 
		sizeof(Read_Config_List[Read_Config_List_Cnt].Weight)); 
	Buffer_Loc += sizeof(Read_Config_List[Read_Config_List_Cnt].Weight);

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Name: malformed buffer\n");
#endif
	    free(Read_Config_List);
	    return EINVAL;
	} /* if */
	if (i)
	    Read_Config_List[Read_Config_List_Cnt].Feature = Buffer_Loc;
	else
	    Read_Config_List[Read_Config_List_Cnt].Feature = NULL;
	Buffer_Loc += i;

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Name: malformed buffer\n");
#endif
	    free(Read_Config_List);
	    return EINVAL;
	} /* if */
	/* List of nodes in the configuration is here */
	Buffer_Loc += i;

	memcpy(&i, Buffer_Loc, sizeof(i)); 
	Buffer_Loc += sizeof(i);
	if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Load_Node_Name: malformed buffer\n");
#else
	    syslog(LOG_ERR, "Load_Node_Name: malformed buffer\n");
#endif
	    free(Read_Config_List);
	    return EINVAL;
	} /* if */
	/* Bitmap of nodes in the configuration is here */
	Buffer_Loc += i;

#if 0
	printf("CPUs=%d, ", Read_Config_List[Read_Config_List_Cnt].CPUs);
	printf("RealMemory=%d, ", Read_Config_List[Read_Config_List_Cnt].RealMemory);
	printf("TmpDisk=%d, ", Read_Config_List[Read_Config_List_Cnt].TmpDisk);
	printf("Weight=%d, ", Read_Config_List[Read_Config_List_Cnt].Weight);
	printf("Feature=%s\n", Read_Config_List[Read_Config_List_Cnt].Feature);
#endif
	Read_Config_List_Cnt++;
    } /* while */

    /* Load and scan the node records */
    while ((Buffer_Loc+sizeof(My_Node_Entry.Name)+(sizeof(int)*2)) <= 
	   (Node_API_Buffer+Node_API_Buffer_Size)) {	
	memcpy(&My_Node_Entry.Name, Buffer_Loc, sizeof(My_Node_Entry.Name)); 
	Buffer_Loc += sizeof(My_Node_Entry.Name);
	if (strlen(Req_Name) == 0) strcpy(Req_Name, My_Node_Entry.Name);

	memcpy(&My_Node_Entry.NodeState, Buffer_Loc, sizeof(My_Node_Entry.NodeState)); 
	Buffer_Loc += sizeof(My_Node_Entry.NodeState);

	memcpy(&Config_Num, Buffer_Loc, sizeof(Config_Num)); 
	Buffer_Loc += sizeof(Config_Num);

#if 0
	printf("Name=%s, ", My_Node_Entry.Name);
	printf("NodeState=%d, ", My_Node_Entry.NodeState);
	printf("Config_Num=%d\n", Config_Num);
#endif

	if (strcmp(My_Node_Entry.Name, Req_Name) != 0) continue;
	*State = My_Node_Entry.NodeState;
	if (Config_Num == 0) {
	    *CPUs 	= 0;
	    *RealMemory	= 0;
	    *TmpDisk	= 0;
	    *Weight	= 0;
	    Features[0]	= NULL;
	} else {
	    Config_Num--;
	    *CPUs 	= Read_Config_List[Config_Num].CPUs;
	    *RealMemory	= Read_Config_List[Config_Num].RealMemory;
	    *TmpDisk	= Read_Config_List[Config_Num].TmpDisk;
	    *Weight	= Read_Config_List[Config_Num].Weight;
	    Features[0]	= Read_Config_List[Config_Num].Feature;
	} /* else */
	if ((Buffer_Loc+sizeof(My_Node_Entry.Name)) <=
	    (Node_API_Buffer+Node_API_Buffer_Size)) 
	    memcpy(Next_Name, Buffer_Loc, sizeof(My_Node_Entry.Name));
	else
	    strcpy(Next_Name, "");

	if (Read_Config_List) free(Read_Config_List);
	return 0;
    } /* while */
    if (Read_Config_List) free(Read_Config_List);
#if DEBUG_SYSTEM
    fprintf(stderr, "Load_Node_Name: Could not locate node %s\n", Req_Name);
#else
    syslog(LOG_ERR, "Load_Node_Name: Could not locate node %s\n", Req_Name);
#endif
    return ENOENT;
} /* Load_Node_Name */


/* 
 * Load_Nodes_Up - Load the bitmap of up nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
int Load_Nodes_Up(unsigned **NodeBitMap, int *BitMap_Size) {
    int i, Config_Num, Version;
    time_t Update_Time;
    char *Buffer_Loc;

    /* Load buffer's header */
    Buffer_Loc = Node_API_Buffer;
    memcpy(&Version, Buffer_Loc, sizeof(Version));
    Buffer_Loc += sizeof(Version);
    memcpy(&Update_Time, Buffer_Loc, sizeof(Update_Time));
    Buffer_Loc += sizeof(Update_Time);

    /* Read up and idle node bitmaps */
    memcpy(&i, Buffer_Loc, sizeof(i)); 
    Buffer_Loc += sizeof(i);

    if ((Buffer_Loc+i) > (Node_API_Buffer+Node_API_Buffer_Size)) return EINVAL;	
    NodeBitMap[0] = (unsigned *)Buffer_Loc;
    *BitMap_Size = i;
    return 0;
} /* Load_Nodes_Up */
#endif
