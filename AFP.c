/*------------------------------------------------------------------------------##	AFP.c : this module contains the functions used to  :#		- log on an AppleShare server (username/password or guest)#		- get the number of volumes and their list#		- get the list of directories with their access rights.#		- get the server time##	Once you have logged on, a session is open, it's up to you to logout.#	Once you have open a volume, you have to close it.###	Versions:	1.0					10/91#	Built with MPW 3.2##	C.Buttin - Apple Computer Europe			#------------------------------------------------------------------------------*/#include <Limits.h>#include <Types.h>#include <ToolUtils.h>#include <Memory.h>#include <OSUtils.h>#include <stdIO.h>#include <String.h>#include <Strings.h>#include <Devices.h>#include <OSUtils.h>#include <errors.h>#include <AppleTalk.h>/* defined used for AppleTalk */#define sync		false			/* synchronous operation */#define quantumSize 578				/* ATP packet size */#define countOffset 4	/* structures used by GetDirectories */typedef struct OffSpringParam {	Byte	length;	Byte	flags;	short	offsetName;	long	dirID;	Byte	UAM[4];}OffSpringParam;typedef OffSpringParam *OffSpringPtr;typedef struct InfoDir {	short 	UAM;				/* access right for this directory */	long 	dirID;					Str32	dirName;}InfoDir;typedef InfoDir *InfoDirPtr;/* C routines declaration. *//*	InitXPP : Open the ATP and XPP drivers	Output : noErr or the error number if an error occurred */pascal OSErr InitXPP(void);/* AFP Calls *//* GetServerInfo :	Get in the reply buffer, information about the server :			- name			- machine type			- UAMs	lnput : AppleTalk server address			Pointer on a reply buffer			length of this buffer	Output : noErr or the error number if an error occurred */pascal OSErr GetServerInfo(AddrBlock *serverAddress,Ptr replyBuffer,short buffLength);/* LogOnwithName :	lnput : AppleTalk address of the server			UserName			Password			Pointer on a SCCBlock, must be of size scbMemSize, must remain valid			and locked during the whole session (i.e. till LogOut)	Output : session number or the error number if an error occurred */pascal short LogOnwithName(AddrBlock *serverAddress,Ptr theName,Ptr thePassword,Ptr SCBBlock);/* LogOnAsGuest :	lnput : AppleTalk address of the server			Pointer on a SCCBlock, must be of size scbMemSize, must remain valid			and locked during the whole session (i.e. till LogOut)	Output : session number or the error number if an error occurred */pascal short LogOnAsGuest(AddrBlock *serverAddress,Ptr SCBBlock);/* GetServerParams :	Get in the reply buffer,the number of volumes 						and the volume list	lnput : Session Reference Number			Pointer on a reply buffer			length of this buffer	Output : noErr or the error number if an error occurred */pascal OSErr GetServerParams(short sessNum,Ptr replyBuffer,short buffLength);/* OpenVolume :	Open an AppleShare volume	lnput : Session Reference Number			volume name	Output : volume number			 error number if an error occurred */pascal short OpenVolume(short sessNum,Ptr volumeName,short* volID);/* GetVolumePrivileges :	Returns the privileges of the volume.	lnput : Session Reference Number			volume ID	Output : volume privileges or error number if an error occurred */pascal short GetVolumePrivileges(short sessionNum,short volumeID);/* GetDirectories :	Returns in a buffer the list of directories :					name	(pascal string)					dirID					access mode	lnput : Session Reference Number			volume ID			directory to be listed			buffer to receive the data			length of this buffer			max number of dirs to be got	Output : error number if an error occurred */pascal OSErr GetDirectories(short sessNum,short volID,long dirID,							Ptr buffer,short buffLength,short reqCount);/* CloseVolume :	Close an AppleShare volume	lnput : Volume Reference Number */pascal void CloseVolume(short sessNum,short volumeID);/* LogOut :	stop the session	lnput : Session Reference Number			Pointer SCBBlock	Output : noErr or the error number if an error occurred */pascal OSErr LogOut(short sessNum,Ptr SCBBlock);/* Server utilities *//* CheckUAM : verify if a User Access Method is available 	Input : string containing the method name			reply buffer got from GetServerInfo	Output : Boolean, true is the method is available */pascal Boolean CheckUAM(const Str32 theMethod,Ptr replyBuffer);/* GetServerTime : extract the server time from the reply                    got from GetServerParams	Input : reply buffer	Output : server time (in seconds, on Macintosh Unit (since Januray 1904) */pascal unsigned long GetServerTime(Ptr replyBuffer);/* Volumes utilities *//* GetNumberVolumes : extract the number of volumes of the server                      from the reply got from GetServerParams	Input : reply buffer	Output : number of volumes */pascal short GetNumberVolumes(Ptr replyBuffer);/* ExtractVolumeName : extract the nth volume from the reply buffer					  got from GetServerParams	Input : reply buffer		    volumeNumber			empty string to receive the volume name (as a Pascal string)	Output : Boolean, false if volume number is too big */pascal Boolean ExtractVolumeName(Ptr replyBuffer,short volumeNumber,Str255 theVol);/* Directories utilities *//* GetNumberDirs : extract the number of directories from the reply got                    from GetDirectories	Input : reply buffer	Output : number of Dirs */pascal short GetNumberDirs(Ptr replyBuffer);/* ExtractDirInfo : extract the info regarding the nth dir from the reply buffer					  got from GetDirectories	Input : reply buffer		    dirNumber			Pointer on a dirInfo structure	Output : Boolean, false if dirNumber is too big */pascal Boolean ExtractDirInfo(Ptr replyBuffer,short dirNumber,InfoDirPtr theDir);							/**** CODE  ****/pascal OSErr InitXPP(void){	short 	drivernum;	OSErr 	error;		/* open .ATP driver and .XPP driver */	if ((error = ATPLoad()) != noErr)		return error;	/* open .XPP driver */	return OpenXPP(&drivernum);} /* InitXPP */  						/* AFP functions */pascal OSErr GetServerInfo(AddrBlock *serverAddress,Ptr replyBuffer,short buffLength){	XPPParamBlock *XPPBlock;				/* to build parameter blocks for AFP */	OSErr		error;	if ((error = InitXPP()) != noErr)		return error;		if (!(XPPBlock = (XPPParamBlock *)NewPtrClear(sizeof(XPPParamBlock))))		return MemError();	XPPBlock->XPP.ioRefNum = xppRefNum;		/* driver reference number */	/* prepare command information : 		no input provided except command - see IA 13-95.		In the Mac inplementation you must use ASPGetStatus (see IM vol.V-541) */ 	XPPBlock->OPEN.serverAddr = *serverAddress;   /* AppleTalk address of the server */	XPPBlock->XPP.aspTimeout = 2;            		/* Timeout for ATP */ 	XPPBlock->XPP.aspRetry = 2; 	XPPBlock->XPP.rbPtr = replyBuffer;     			/* Reply buffer pointer */	XPPBlock->XPP.rbSize = buffLength;	error = ASPGetStatus((XPPParmBlkPtr)XPPBlock,sync);	DisposPtr((Ptr)XPPBlock);	return error; } /* GetServerInfo *//* Log on server */pascal short LogOnwithName(AddrBlock *serverAddress,Ptr theName,Ptr thePassword,Ptr SCBBlock){	AFPLoginPrm *XPPBlock;							/* to build parameter blocks for AFP */	short 		i,lgInfo,cbsize;	char		XPPReply[quantumSize*8];			/* used to get AFP replies (max. size 8 ATP packet) */	char		XPPCmd[quantumSize];				/* used to send AFP commands */	OSErr		error;	short		result;	char		userAuthInfo[50];		/* information used by Login function */	char		AFPVersion[30];	char		AuthentMethod[30];		strcpy(AFPVersion, "\pAFPVersion 2.0");	strcpy(AuthentMethod, "\pCleartxt passwrd");	/* prepare user info */	lgInfo = (short)theName[0]+1;	BlockMove(theName,userAuthInfo,lgInfo);	if (lgInfo % 2 == 0)	{		/* pad with a null byte to have password on even boundary */		lgInfo++;		userAuthInfo[lgInfo] = '\0';		}	for (i = 1; i <= thePassword[0]; i++,lgInfo++) 			userAuthInfo[lgInfo] = thePassword[i];	for (; i <= 8; i++,lgInfo++) 				/* pad with null bytes if necessary */			userAuthInfo[lgInfo] = '\0';			if (!(XPPBlock = (AFPLoginPrm *)NewPtrClear(sizeof(AFPLoginPrm))))		return MemError();		XPPBlock->ioRefNum = xppRefNum;    XPPBlock->ioCompletion = nil; 	XPPBlock->aspTimeout = 1;            		/* Timeout for ATP */ 	XPPBlock->aspRetry = 2;              		/* Retry count for ATP */ 	XPPBlock->afpAddrBlock = *serverAddress;    /* AppleTalk address of the server */ 	XPPBlock->afpAttnRoutine = nil ;            	XPPBlock->rbPtr = (Ptr)XPPReply;       		/* Reply buffer pointer */	XPPBlock->rbSize = quantumSize;			 	/* Reply buffer size */		/* prepare command information :	   1st byte : command (login)	   2nd Pascal string : AFP version	   3rd Pascal string : Authentication Method - see Inside AppleTalk 13-105	   4th user information : username - password */	XPPCmd[0] = afpLogin;	cbsize = 1;	BlockMove(AFPVersion,XPPCmd+cbsize,AFPVersion[0]+1);	cbsize += AFPVersion[0]+1;	BlockMove(AuthentMethod,XPPCmd+cbsize,AuthentMethod[0]+1);	cbsize += AuthentMethod[0]+1;	/* add user info */	BlockMove(userAuthInfo,XPPCmd+cbsize,lgInfo);	cbsize += lgInfo;	    XPPBlock->cbPtr = (Ptr)XPPCmd;        	 	/* Command block pointer */	XPPBlock->cbSize = cbsize;	XPPBlock->afpSCBPtr = (Ptr) SCBBlock; 		/* SCB pointer in AFP login */	if ((error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync)) != noErr)		result = error;	else		if (XPPBlock->cmdResult != noErr)		   result =  XPPBlock->cmdResult ;	  	else result =  XPPBlock->sessRefnum;			/* return session number */		DisposPtr((Ptr)XPPBlock);	return result;} /* LogOnwithName */pascal short LogOnAsGuest(AddrBlock *serverAddress,Ptr SCBBlock)  {	AFPLoginPrm *XPPBlock;							/* to build parameter blocks for AFP */	short 		cbsize;	char		XPPReply[quantumSize*8];			/* used to get AFP replies (max. size 8 ATP packet) */	char		XPPCmd[quantumSize];				/* used to send AFP commands */	OSErr		error;	short		result;	/* information used by Login function */	char		AFPVersion[30];	char		AuthentMethod[30];		strcpy(AFPVersion, "AFPVersion 2.0");	strcpy(AuthentMethod, "No User Authent");		if (!(XPPBlock = (AFPLoginPrm *)NewPtrClear(sizeof(AFPLoginPrm))))		return MemError();		XPPBlock->ioRefNum = xppRefNum;    XPPBlock->ioCompletion = nil; 	XPPBlock->aspTimeout = 1;            		/* Timeout for ATP */ 	XPPBlock->aspRetry = 2;              		/* Retry count for ATP */ 	XPPBlock->afpAddrBlock = *serverAddress;    /* AppleTalk address of the server */ 	XPPBlock->afpAttnRoutine = nil ;            	XPPBlock->rbPtr = (Ptr)XPPReply;       		/* Reply buffer pointer */	XPPBlock->rbSize = quantumSize;			 	/* Reply buffer size */		/* prepare command information :	   1st byte : command (login)	   2nd Pascal string : AFP version	   3rd Pascal string : Authentication Method - see Inside AppleTalk 13-104 */	XPPCmd[0] = afpLogin;	XPPCmd[1] = strlen(AFPVersion);	strcpy(XPPCmd+2,AFPVersion);	cbsize = strlen(XPPCmd);	XPPCmd[cbsize] = strlen(AuthentMethod);	strcpy(XPPCmd+cbsize+1,AuthentMethod);	cbsize = strlen(XPPCmd);	    XPPBlock->cbPtr = (Ptr)XPPCmd;        	 	/* Command block pointer */	XPPBlock->cbSize = cbsize;	XPPBlock->afpSCBPtr = (Ptr) SCBBlock; 		/* SCB pointer in AFP login */	if ((error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync)) != noErr)		result = error;	else		if (XPPBlock->cmdResult != noErr)		   result =  XPPBlock->cmdResult ;	  	else result =  XPPBlock->sessRefnum;			/* return session number */		DisposPtr((Ptr)XPPBlock);	return result;}	/* LogOnAsGuest */		/* get the server parameters (AFP call : GetSrvParms) */pascal OSErr GetServerParams(short sessNum,Ptr replyBuffer,short buffLength){	XPPPrmBlk 	*XPPBlock;				/* to build parameter blocks for AFP */	char		XPPCmd[quantumSize];	/* used to send AFP commands */	OSErr		error;		if (!(XPPBlock = (XPPPrmBlk *)NewPtrClear(sizeof(XPPPrmBlk))))		return MemError();	XPPBlock->ioRefNum = xppRefNum;		/* driver reference number */	/* prepare command information : 		no input provided except command - see IA 13-98 */ 	XPPCmd[0]  = afpGetSParms;		XPPBlock->sessRefnum = sessNum;	XPPBlock->aspTimeout = 2;            /* Timeout for ATP */ 	XPPBlock->aspRetry = 2; 	XPPBlock->cbPtr = (Ptr)XPPCmd;    XPPBlock->cbSize = 1;               /* Command block size */	XPPBlock->rbPtr = replyBuffer;      /* Reply buffer pointer */	XPPBlock->rbSize = buffLength;  	XPPBlock->wdSize = 0;               /* Write Data size */    XPPBlock->wdPtr = nil;              /* Write Data pointer */		error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync);	DisposPtr((Ptr)XPPBlock);	return error; }/* Open an AppleShare volume */pascal OSErr OpenVolume(short sessNum,Ptr volumeName,short* volID){	XPPPrmBlk 	*XPPBlock;				/* to build parameter blocks for AFP */	char		XPPCmd[quantumSize];	/* used to send AFP commands */	OSErr		error;	char		replyBuffer[100];		if (!(XPPBlock = (XPPPrmBlk *)NewPtrClear(sizeof(XPPPrmBlk))))		return MemError();	XPPBlock->ioRefNum = xppRefNum;		/* driver reference number */	/* prepare command information :		command		bitmap		volume name as a pascal string - see IA 13-121 */ 	XPPCmd[0]  = afpOpenVol;	XPPCmd[1]  = 0;	XPPCmd[2] = 0;	XPPCmd[3] = 0x20;	/* we just want the volume ID (bit 5 in bitmap) */	BlockMove(volumeName,XPPCmd+4,volumeName[0]+1);	XPPBlock->sessRefnum = sessNum;	XPPBlock->aspTimeout = 2;            	/* Timeout for ATP */ 	XPPBlock->aspRetry = 2; 	XPPBlock->cbPtr = (Ptr)XPPCmd;    XPPBlock->cbSize = volumeName[0]+5;    	/* Command block size */	XPPBlock->rbPtr = replyBuffer;     		/* Reply buffer pointer */	XPPBlock->rbSize = 100;  	XPPBlock->wdSize = 0;               	/* Write Data size */    XPPBlock->wdPtr = nil;             		/* Write Data pointer */		if ((error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync)) == noErr)		if (XPPBlock->cmdResult != noErr)  			error =  XPPBlock->cmdResult ;		else {		/* volume parameters are contained within the reply buffer		   the volume ID is at byte 2 and it's two bytes long 		   (see Inside AppleTalk pp 13-102 && 13-121 */			BlockMove(replyBuffer+ 2,volID,2);	/* return volume number */			}	if (error)		*volID = -1;	DisposPtr((Ptr)XPPBlock);	return error; }	/* OpenVolume *//* getting the volume privileges is the same as getting the root privileges */pascal short GetVolumePrivileges(short sessionNum,short volumeID){	XPPPrmBlk 		*XPPBlock;				/* to build parameter blocks for AFP */	char			XPPCmd[quantumSize];	/* used to send AFP commands */	OSErr			error;	short			lg;	Byte			buffer[100];	long			dirID = 2;				/* the root has the dirID 2 */	Byte			UAM[4];		if (!(XPPBlock = (XPPPrmBlk *)NewPtrClear(sizeof(XPPPrmBlk))))		return MemError();		XPPBlock->ioRefNum = xppRefNum;		/* driver reference number */	/* prepare command information :		command	: FPGetFileDirParms			see IA - 13-83 		0		volumeID		DirectoryID			of the directory we are looking for		File BitMap			not used		Directory BitMap		PathType		PathName			not used (must be set to empty string) 	*/  	lg = 0;	XPPCmd[lg++]  = afpGetFlDrParms;	XPPCmd[lg++]  = 0;	BlockMove(&volumeID,XPPCmd+lg,2);		/* volumeID */	lg += 2;	BlockMove(&dirID,XPPCmd+lg,4);			/* dirID */	lg += 4;	XPPCmd[lg++] = 0;						/* File BitMap */	XPPCmd[lg++] = 0;						/* File BitMap */	XPPCmd[lg++] = 0x10;					/* Directory BitMap : Access Rights */	XPPCmd[lg++] = 0;						/* Directory BitMap */	XPPCmd[lg++] = 2;						/* pathname is long name */	XPPCmd[lg++] = '\0';					/* pathname is empty */			XPPBlock->sessRefnum = sessionNum;	XPPBlock->aspTimeout = 2;            	/* Timeout for ATP */ 	XPPBlock->aspRetry = 2; 	XPPBlock->cbPtr = (Ptr)XPPCmd;    XPPBlock->cbSize = lg;    				/* Command block size */	XPPBlock->rbPtr = buffer;     			/* Reply buffer pointer */	XPPBlock->rbSize = 100;  	XPPBlock->wdSize = 0;               	/* Write Data size */    XPPBlock->wdPtr = nil;             		/* Write Data pointer */		if ((error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync)) == noErr)		if (XPPBlock->cmdResult != noErr)  			error =  XPPBlock->cmdResult ;	/* the access rights are set up after the file and dir bitmaps		+ 2 bytes of flags */	BlockMove(buffer + 6,UAM,4);	/* return the first octet as a short */	lg = (short)UAM[0];	DisposPtr((Ptr)XPPBlock);	return ((error == noErr) ? lg : error);} /* GetVolumePrivileges *//* Get the list of subdirectories of a directory with their name,    dirID and  privileges */pascal OSErr GetDirectories(short sessNum,short volID,long dirID,							Ptr buffer,short buffLength,short reqCount){	XPPPrmBlk 		*XPPBlock;				/* to build parameter blocks for AFP */	char			XPPCmd[quantumSize];	/* used to send AFP commands */	OSErr			error;	short			temp,lg;		if (!(XPPBlock = (XPPPrmBlk *)NewPtrClear(sizeof(XPPPrmBlk))))		return MemError();	/* we expect to receive as reply, for each dir in the directory :		- the offset to dirname	(short)		- the dirID	(long)		- the Access rights of the dir (long)		- the name of the dir (Str32) */	if ((reqCount * 50) > buffLength)		return afpParmErr;					/* buffer too small */		XPPBlock->ioRefNum = xppRefNum;		/* driver reference number */	/* prepare command information :		command	: FPEnumerate			see  IA 13-73		0		volumeID		DirectoryID				to be browsed		File BitMap				not used		Directory BitMap		dirName + dirID + access rights		ReqCount		StartIndex		MaxReplySize		PathType		PathName				not used (must be set to empty string) */ 	lg = 0;	XPPCmd[lg++]  = afpEnumerate;	XPPCmd[lg++]  = 0;	BlockMove(&volID,XPPCmd+lg,2);			/* volumeID */	lg += 2;	BlockMove(&dirID,XPPCmd+lg,4);			/* parID */	lg += 4;	XPPCmd[lg++] = 0;						/* File BitMap */	XPPCmd[lg++] = 0;						/* File BitMap */	XPPCmd[lg++] = 0x11;					/* Directory BitMap : Access Rights + dirID */	XPPCmd[lg++] = 0x40;					/* Directory BitMap : names */	BlockMove(&reqCount,XPPCmd+lg,2);		/* requested count */	lg += 2;	temp = 1;								/* start index */	BlockMove(&temp,XPPCmd+lg,2);	lg += 2;	temp = reqCount * 50;					/* maximum size of reply block */	BlockMove(&temp,XPPCmd+lg,2);	lg += 2;	XPPCmd[lg++] = 2;						/* pathname is long name */	XPPCmd[lg++] = '\0';					/* pathname is empty */			XPPBlock->sessRefnum = sessNum;	XPPBlock->aspTimeout = 2;            	/* Timeout for ATP */ 	XPPBlock->aspRetry = 2; 	XPPBlock->cbPtr = (Ptr)XPPCmd;    XPPBlock->cbSize = lg;    				/* Command block size */	XPPBlock->rbPtr = buffer;     			/* Reply buffer pointer */	XPPBlock->rbSize = buffLength;  	XPPBlock->wdSize = 0;               	/* Write Data size */    XPPBlock->wdPtr = nil;             		/* Write Data pointer */		if ((error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync)) == noErr)		if (XPPBlock->cmdResult != noErr)  			error =  XPPBlock->cmdResult ;	DisposPtr((Ptr)XPPBlock);	return error;}	/* GetDirectories *//* close an AppleShare volume */pascal void CloseVolume(short sessNum,short volumeID){	XPPPrmBlk 	*XPPBlock;				/* to build parameter blocks for AFP */	char		XPPCmd[quantumSize];	/* used to send AFP commands */	OSErr		error;		if (!(XPPBlock = (XPPPrmBlk *)NewPtrClear(sizeof(XPPPrmBlk))))		return ;	XPPBlock->ioRefNum = xppRefNum;		/* driver reference number */	/* prepare command information :		command  1 byte		0		 1 byte		volumeID 2 bytes 		see IA 13-63 */ 	XPPCmd[0]  = afpVolClose;	XPPCmd[1] = 0;	BlockMove(&volumeID,XPPCmd+2,2);	XPPBlock->sessRefnum = sessNum;	XPPBlock->aspTimeout = 2;            	/* Timeout for ATP */ 	XPPBlock->aspRetry = 2; 	XPPBlock->cbPtr = (Ptr)XPPCmd;    XPPBlock->cbSize = 4;    				/* Command block size */	XPPBlock->rbPtr = nil;     				/* Reply buffer pointer */	XPPBlock->rbSize = 0;  	XPPBlock->wdSize = 0;               	/* Write Data size */    XPPBlock->wdPtr = nil;             		/* Write Data pointer */		error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync);	DisposPtr((Ptr)XPPBlock); }  /* CloseVolume *//* Close the session */pascal OSErr LogOut(short sessNum,Ptr SCBBlock){#pragma unused (SCBBlock)	XPPPrmBlk 	*XPPBlock;				/* to build parameter blocks for AFP */	char		XPPCmd[quantumSize];	/* used to send AFP commands */	OSErr		error;		if (!(XPPBlock = (XPPPrmBlk *)NewPtrClear(sizeof(XPPPrmBlk))))		return MemError();	XPPBlock->ioRefNum = xppRefNum;		/* driver reference number */ 	/* prepare command information :		just the command 	see IA 13-108 */ 	XPPCmd[0]  = afpLogout;	XPPBlock->cbPtr = (Ptr)XPPCmd;          /*Command block pointer */	XPPBlock->cbSize = 1;   	XPPBlock->sessRefnum = sessNum;	XPPBlock->aspTimeout = 2;            /*Timeout for ATP */	XPPBlock->aspRetry = 2; 	error = AFPCommand((XPPParmBlkPtr)XPPBlock,sync);	DisposPtr((Ptr)XPPBlock);	return error;	}	/* LogOut */					/*** 		UTILITIES			***//* Check if a UAM is available */pascal Boolean CheckUAM(const Str32 theMethod,Ptr replyBuffer){	short 	offset,i,j,numUAMs,length;	Ptr		p;		BlockMove(replyBuffer+4, &offset,2);	/* to get the offset of UAMs in the buffer */	numUAMs = (short)(*(replyBuffer + offset));	/* UAMs are packed as Pascal string. By comparing the strings,		   we can see if find the right UAM */	p =	replyBuffer + offset + 1;	for (i = 1, length = (short)*p; i <= numUAMs ;i++)	{		for (j = 1; j <= length;j++)			if (theMethod[j] != p[j])				break;		if (j > length)					/* we found it */			return true;		else 			if (i < numUAMs) 	{				p += length+1;				length = (short)*p;				}		}	return false;}	/* CheckUAM *//* calculate the server time in seconds in Macintosh units */					pascal unsigned long GetServerTime(Ptr replyBuffer){	long 		theTime,theLocalTime;	DateTimeRec theDate;		/* the Volumes buffer contains :		see IA p 13-99			- server time (long) : this value is the number of seconds			  measured from  12:00 am on Januray 1,2000 */		BlockMove(replyBuffer,&theTime,4);	/* calculate the number of seconds till Januray 1, 2000 */	theDate.day = 1;	theDate.month = 1;	theDate.year = 2000;	theDate.hour = 0;	theDate.minute = 0;	theDate.second = 0;	Date2Secs(&theDate,(unsigned long* )&theLocalTime);	/* add the two values to get the right time */	theTime += theLocalTime;	return theTime;	} /* GetServerTime *//* Get the number of volumes of an AppleShare server */pascal short GetNumberVolumes(Ptr replyBuffer){	short numVol;	/* the Volumes buffer contains :		see IA p 13-99			- server time	(long)			- num of vols.	(short) */				numVol = (short)*(replyBuffer+4);	return numVol;	} /* GetNumberVolumes *//* extract from the AFP reply buffer a volume name as a pascal string */pascal Boolean ExtractVolumeName(Ptr replyBuffer,short volumeNumber,char *theVol){	/* the structure of the buffer is defined as follow (see Inside AppleTalk	   p 13-98) :	   	1 byte for flags (volume has password )		Volume name as a Pascal string. Volume names are delimited by a null char		if even number of chars.*/			short i,j,length;		if (volumeNumber > GetNumberVolumes(replyBuffer))		return false;	replyBuffer = replyBuffer+5;					/* position at 1st volume name */	for (i = 1; i <= volumeNumber;i++) {		length = (short)(*(++replyBuffer));			/* skeep flag byte */		for (j = 0; j <= length;j++,replyBuffer++)	/* volume Name */			if (i == volumeNumber)				theVol[j] = *replyBuffer;		}	return true;} /* ExtractVolumeName *//* Directories utilities *//* Get the number of subdirectories of a directory */pascal short GetNumberDirs(Ptr replyBuffer){	short count;		/* the GetDirectories buffer contains :		see IA p 13-76			- file BitMap	(short)			- dirBitMap		(short)			- count			(short) */				BlockMove(replyBuffer+4,&count,2);	return count;				} /* GetNumberDirs *//* Get the name, dirID and access rights of a directory */pascal Boolean ExtractDirInfo(Ptr p,short dirNumber,InfoDirPtr theDir){	short	i,length;		/* verify if dirNumber is OK */	if (dirNumber > GetNumberDirs(p))		return false;	/* skip countInfo in the buffer */ 	p += 6;	for (i = 1; i < dirNumber;i++)		/* skip first dirs */ 		p = p+((OffSpringPtr)p)->length;				/* we expect to receive in reply, for each directory :		- the offset to dirname		- the dirID	(long)		- the Access rights of the dir (long)		- the name of the dir (Str32) 		see IA 13-74 */	theDir->UAM = (short)(((OffSpringPtr)p)->UAM[0]);	theDir->dirID = ((OffSpringPtr)p)->dirID;	/* skip first bytes (lenght,flags) */	length = ((OffSpringPtr)p)->offsetName + 2;		p += length;	BlockMove(p,theDir->dirName,*p+1);	return true;} /* ExtractDirInfo */