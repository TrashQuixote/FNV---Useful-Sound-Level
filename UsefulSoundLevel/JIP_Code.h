
#pragma once
#include "GameForms.h"
#include "GameObjects.h"
#include "NiPoint.h"

#define ADDR_ReturnTrue			0x8D0360

class BSTaskletData
{
public:
	virtual void	Destroy(bool doFree);
	virtual bool	Unk_01(void);	// Returns true
	virtual void	ExecuteQueuedTasks();
	virtual void	Unk_03(void);	// Does nothing

	UInt8		byte04;		// 04
	UInt8		pad05[3];	// 05
};

// 14
class BSWin32TaskletData : public BSTaskletData
{
public:
	UInt32		unk08;		// 08
	UInt8		byte0C;		// 0C
	UInt8		pad0D[3];	// 0D
	UInt32		unk10;		// 10
};

// 14
class BSTCommonLLMessageQueue
{
public:
	virtual void	Destroy(bool doFree);
	virtual void	Unk_01(void);
	virtual void	Unk_02(void);
	virtual void	Unk_03(void);
	virtual void	Unk_04(void);
	virtual void	Unk_05(void);
	virtual void	Unk_06(void);

	UInt32		unk04[4];	// 04
};

// 38
class MobileObjectTaskletData : public BSWin32TaskletData
{
public:
	virtual void	EnterCriticalSections(UInt32 unused);
	virtual void	ExecuteTask(TESObjectREFR* execOn);
	virtual void	LeaveCriticalSections(UInt32 unused);

	UInt32					unk14[2];		// 14
	BSTCommonLLMessageQueue	messageQueue;	// 1C
	UInt32					unk30[2];		// 30
};

// 40
class DetectionTaskData : public MobileObjectTaskletData
{
public:
	float			flt38;		// 38
	UInt32			unk3C;		// 3C
};

// 38
class AnimationTaskData : public MobileObjectTaskletData
{
public:
};

// 44
class PackageUpdateTaskData : public MobileObjectTaskletData
{
public:
	UInt32			unk38[3];	// 38
};

// 3C
class ActorUpdateTaskData : public MobileObjectTaskletData
{
public:
	UInt32			unk38;		// 38
};

// 40
class ActorsScriptTaskData : public MobileObjectTaskletData
{
public:
	UInt32			unk38[2];	// 38
};

// 3C
class MovementTaskData : public MobileObjectTaskletData
{
public:
	UInt32			unk38;		// 38
};

struct LipTask;

// Straight from JIP. 
struct ProcessManager
{
	UInt32									unk000;				// 000
	NiTArray<void*>					objects;			// 004
	UInt32									beginOffsets[4];	// 014	0: High, 1: Mid-High, 2: Mid-Low, 3: Low
	UInt32									endOffsets[4];		// 024
	UInt32									offsets034[4];		// 034	Same as beginOffsets
	UInt32									unk044[5];			// 044
	tList<void>								list058;			// 058
	tList<void>						tempEffects;		// 060
	tList<void>						muzzFlashList;		// 068
	tList<void>								list070;			// 070
	tList<void>								list078;			// 078
	tList<Actor>							highActors;			// 080
	Actor* nearestActors[50];	// 088
	UInt32									nearestActorsCount;	// 150
	float									commentPCActionsTimer;	// 154
	float									commentPCKnockingTimer;	// 158
	UInt8									pcInRadiation1;		// 15C
	UInt8									pcInRadiation2;		// 15D
	UInt8									pad15E[2];			// 15E
	DetectionTaskData						detectionTasks;		// 160
	AnimationTaskData						animationTasks;		// 1A0
	PackageUpdateTaskData					packageUpdTasks;	// 1D8
	ActorUpdateTaskData						actorUpdTasks;		// 21C
	ActorsScriptTaskData					actorScriptTasks;	// 258
	MovementTaskData						movementTasks;		// 298
	UInt32									unk2D4[0x4023];		// 2D4
	//LockFreeMap<MobileObject*, LipTask*>	lipTasksMap;		// 10360
	UInt64									lipTasksMapPad[8];	// 10360
	UInt8									toggleAI;			// 103A0
	UInt8									toggleDetection;	// 103A1
	UInt8									toggleDetectionStats;	// 103A2
	UInt8									byte103A3;			// 103A3
	UInt32									detectionStats;		// 103A4
	UInt8									toggleHighProcess;	// 103A8
	UInt8									toggleLowProcess;	// 103A9
	UInt8									toggleMidHighProcess;	// 103AA
	UInt8									toggleMidLowProcess;	// 103AB
	UInt8									toggleAISchedules;	// 103AC
	UInt8									showSubtitle;		// 103AD
	UInt8									byte103AE;			// 103AE
	UInt8									byte103AF;			// 103AF
	UInt32									numHighActors;		// 103B0
	float									crimeUpdateTimer;	// 103B4
	UInt32									crimeNumber;		// 103B8
	float									removeDeadActorsTime;	// 103BC
	UInt32									unk103C0[3];		// 103C0

};


class DetectionData
{
public:
	Actor* actor;				// 00
	UInt8		detectionLevel;		// 04
	UInt8		byte05;				// 05
	UInt8		byte06;				// 06
	UInt8		byte07;				// 07
	SInt32		detectionValue;		// 08
	NiPoint3	detectedLocation;	// 0C
	float		fTimeStamp;			// 18
	UInt8		forceResetCombatLOSBuffer;	// 1C
	UInt8		byte1D;				// 1D
	bool		inLOS;				// 1E
	UInt8		byte1F;				// 1F
	UInt32		unk20;				// 20
};


