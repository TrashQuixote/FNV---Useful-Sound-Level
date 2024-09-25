#pragma once
#include "nvse/PluginAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/GameProcess.h"
#include "nvse/SafeWrite.h"
#include "nvse/NiObjects.h"
#include "internal/class_vtbls.h"
#include "JIP_Code.h"
#include <set>
#include <random>
#include <array>
#include "RoughINIReader.h"
#include <GameData.h>
#include <CommandTable.h>

#define IS_ID(form, type) (form->typeID == kFormType_##type)
#define UsefulSoundLevelDebug 0
//#include "utilities/IConsole.h"
//NoGore is unsupported in xNVSE

/* �� Copied From JIP ��  */
#define ADDR_ReturnTrue	0x8D0360
#define IS_ACTOR(form) ((*(UInt32**)form)[0x100 >> 2] == ADDR_ReturnTrue)
/* �� Copied From JIP �� */

IDebugLog		gLog("UsefulSoundLevel.log");
PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
NVSEMessagingInterface* g_messagingInterface{};
NVSEInterface* g_nvseInterface{};

bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
	_MESSAGE("query");

	// fill out the info structure
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "UsefulSoundLevel";
	info->version = 114;

	// version checks
	if (nvse->nvseVersion < PACKED_NVSE_VERSION)
	{
		_ERROR("NVSE version too old (got %08X expected at least %08X)", nvse->nvseVersion, PACKED_NVSE_VERSION);
		return false;
	}

	if (!nvse->isEditor)
	{
		if (nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525)
		{
			_ERROR("incorrect runtime version (got %08X need at least %08X)", nvse->runtimeVersion, RUNTIME_VERSION_1_4_0_525);
			return false;
		}

		if (nvse->isNogore)
		{
			_ERROR("NoGore is not supported");
			return false;
		}
	}
	else
	{
		if (nvse->editorVersion < CS_VERSION_1_4_0_518)
		{
			_ERROR("incorrect editor version (got %08X need at least %08X)", nvse->editorVersion, CS_VERSION_1_4_0_518);
			return false;
		}
	}

	// version checks pass
	// any version compatibility checks should be done here
	return true;
}

class LoadedModFinder_NoExt
{
	const char* m_stringToFind;

public:
	LoadedModFinder_NoExt(const char* str) : m_stringToFind(str) { }

	bool Accept(ModInfo* modInfo) {
		std::string_view sv_mod_name(modInfo->name);
		sv_mod_name.remove_suffix(4);	// remove .esm/.esp
		std::string s_mod_name{ sv_mod_name.substr(0, sv_mod_name.size()) };
		return !StrCompare(s_mod_name.c_str(), m_stringToFind);
	}
};

// for name which no esp/esm extension
static const ModInfo* LookupModByName_NoExt(const char* modName_noExt)
{
	return DataHandler::Get()->modList.modInfoList.Find(LoadedModFinder_NoExt(modName_noExt));
}

// will return 0xff(wrong index) if not success
UINT32 static inline GetU32ModIndex(const std::string& mod_name) {
	const auto* mod = LookupModByName_NoExt(mod_name.c_str());
	if (!mod || NULL == mod) {
		gLog.FormattedMessage("mod %s not found", mod_name.c_str());
		return 0xff000000;
	}

	return (mod->modIndex << 24);
}

// return form id when formexist,or return 0
UINT32 static inline FormExist(const UINT32& u32_mod_index, const std::string& str_formid) {
	int formid = -1;
	try {
		formid = std::stoi(str_formid, 0, 16);
	}
	catch (...) {
		gLog.FormattedMessage("%s is not valid formid", str_formid);
		return 0;
	}
	if (!formid || formid > 0x00ffffff) {
		gLog.FormattedMessage("%s is not valid formid", str_formid);
		return 0;
	}
	formid = u32_mod_index + formid;

	const auto* form = LookupFormByID(formid);
	if (!form) {
		gLog.FormattedMessage("form %x is not found", formid);
		return 0;
	}
	return formid;
}


namespace SoundLevelMatter {
	using namespace roughinireader;
#define Square(x) ( (x) * (x) )
#define CalcDistanceSquare(x1,y1,z1,x2,y2,z2) ( Square(x1-x2) + Square(y1-y2) +Square(z1-z2) )

	static UINT32 SavedGetSoundLevelAmountFunc;
	static UINT8 g_my_detection_flag = 0;	// |0 - mode |0 0 0 | 0 - DisableDetectionEvent | 0 - work for creature | 0 - work for teammate | 0 - only for pc |
	static UINT32 g_distance_for_pc = 8192;
	static UINT32 g_distance_for_npc = 2048;
	static UINT32 g_distance_for_creature = 1280;
	static UINT32 g_distance_for_Abomination = 2048;
	static UINT32 g_distance_for_Supermutant = 8192;
	static UINT32 g_distance_for_Robot = 8192;

	static double g_VanillaDetectionDistance = 8192;
	static UINT8 s_sounlev_minvol = 10;

	struct radiation_data {
		float detection_val_to_PC;
		Actor* centre;

		__forceinline bool operator==(const radiation_data& _rhs) const { return centre == _rhs.centre; } 
		__forceinline bool operator<(const radiation_data& _rhs) const { return detection_val_to_PC > _rhs.detection_val_to_PC; }
		__forceinline bool InRangeSquare (const NiVector3& _pos, float distance_square) const {
			return CalcDistanceSquare(centre->posX, centre->posY, centre->posZ, _pos.x, _pos.y, _pos.z) < distance_square;
		}
		__forceinline bool InRange(const Actor* _actor, float distance) const {
			return InRangeSquare(NiVector3{ _actor->posX,_actor->posY,_actor->posZ }, Square(distance));
		}
		__forceinline void Set(float new_detection_val, Actor* new_centre) {
			detection_val_to_PC = new_detection_val;
			centre = new_centre;
		}
	};
	
	struct SoundLevelMng {
		using weap_sound_map = std::unordered_map<UINT32, UINT8>;
		using proj_sound_map = std::unordered_map<UINT32, UINT8>;
		
		float generic_radiation_distance;
		float generic_detection_scale;
		UINT32 generic_subsonic_speed;
		UINT8 generic_sound_volume;
		UINT8 generic_silence_decrement;
		UINT8 generic_suppressor_decrement;
		UINT8 generic_subsonic_decrement;
		weap_sound_map wp_sound_map{};
		proj_sound_map pj_sound_map{};
		SoundLevelMng(SoundLevelMng&&) = delete;
	private:
		SoundLevelMng() {
			generic_radiation_distance = 2048;
			generic_subsonic_speed = 0;
			generic_sound_volume = 0;
			generic_silence_decrement = 0;
			generic_suppressor_decrement = 0;
			generic_subsonic_decrement = 0;
		}
		~SoundLevelMng() {}
		//SoundLevelMng& operator=(const SoundLevelMng&) = delete;
	public:

		static SoundLevelMng& GetSingleton() {
			static SoundLevelMng singleton;
			return singleton;
		}
	};
#define SOUND_LEV_MNG SoundLevelMng::GetSingleton()
#define SOUND_LEV_MNG_PJMAP SoundLevelMng::GetSingleton().pj_sound_map
#define SOUND_LEV_MNG_WPMAP SoundLevelMng::GetSingleton().wp_sound_map

	static inline void PrintSoundLevelMng() {
		gLog.FormattedMessage("generic_distance_pc %u", g_distance_for_pc);
		gLog.FormattedMessage("generic_distance_npc %u", g_distance_for_npc);
		gLog.FormattedMessage("Vanilla_Max_Detection_distance %f", g_VanillaDetectionDistance);
		gLog.FormattedMessage("detection_flag %u", g_my_detection_flag);
		gLog.FormattedMessage("generic_sound_volume %u", SOUND_LEV_MNG.generic_sound_volume);
		gLog.FormattedMessage("generic_silence_decrement %u", SOUND_LEV_MNG.generic_silence_decrement);
		gLog.FormattedMessage("generic_suppressor_decrement %u", SOUND_LEV_MNG.generic_suppressor_decrement);
		gLog.FormattedMessage("generic_subsonic_speed %u", SOUND_LEV_MNG.generic_subsonic_speed);
		gLog.FormattedMessage("generic_subsonic_decrement %u", SOUND_LEV_MNG.generic_subsonic_decrement);
	}

	static inline void PrintSoundLevelMngPJ() {
		for (auto iter = SOUND_LEV_MNG_PJMAP.begin(); iter != SOUND_LEV_MNG_PJMAP.end(); iter++) {
			gLog.FormattedMessage("PJ Formid %x, Value %d", iter->first, iter->second);
		}
	}

	static inline void PrintSoundLevelMngWp() {
		for (auto iter = SOUND_LEV_MNG_WPMAP.begin(); iter != SOUND_LEV_MNG_WPMAP.end(); iter++) {
			gLog.FormattedMessage("Wp Formid %x, Value %u", iter->first, iter->second);
		}
	}
#define PCOnly 0b00000001
#define ApplyToTeammate 0b00000010
#define CreatureCanDetection 0b00000100
#define DisableDetectionEvent 0b00001000
#define RadiationDetection 0b00010000
#define RealisticMode 0b10000000

	// return true when the flag is on
	static __forceinline bool IsFlagOn(UINT8 _flag) {
		return (g_my_detection_flag & _flag) != 0;
	}


	static __forceinline void CreateDetectionEvent(BaseProcess* _this, Actor* _actor, float pX, float pY, float pZ,
		UINT32 soundLevel, UINT32 eventtype, TESObjectREFR* locationRef) {
		//CreateDetectionEvent
		ThisStdCall(0x903E40, _this, _actor, pX, pY, pZ, soundLevel, eventtype, locationRef);
	}


	static __forceinline BGSProjectile* GetBaseProjOfCurrentEqAmmo(TESObjectWEAP* _this, Actor* _actor)
	{
		return ThisStdCall<BGSProjectile*>(0x525A90, _this, _actor);
	}

	static INT32 __fastcall GetSoundLevelAmountFunc_8BAE42(TESObjectWEAP* _this, Actor* _actor, UINT32 has_sil, UINT32 has_supp)
	{
		INT32 ret = ThisStdCall<INT32>(SavedGetSoundLevelAmountFunc, _this, has_sil, has_supp);	// In fact I don't care what you return :)
		
		
		if (!_actor || !IsFlagOn(RealisticMode)) return ret;
		ret = SOUND_LEV_MNG.generic_sound_volume;

		INT32 sil_supp_buff = ((has_supp > 0) * (SOUND_LEV_MNG.generic_suppressor_decrement));
		if (has_sil > 0) sil_supp_buff = ((has_sil > 0) * (SOUND_LEV_MNG.generic_silence_decrement));

		// check weapon at first
		if (const auto& iter = SOUND_LEV_MNG_WPMAP.find(_this->refID); iter != SOUND_LEV_MNG_WPMAP.end()) {
			sil_supp_buff = iter->second;
		}
		// check weapon done

#if UsefulSoundLevelDebug
		gLog.FormattedMessage("New Sound Level %u", ret);
#endif	//VariousSoundLevelDebug

		if (sil_supp_buff == 0) return ret;								// No Sil and Supp, return
		auto cur_baseproj = GetBaseProjOfCurrentEqAmmo(_this, _actor);

#if UsefulSoundLevelDebug
		auto deal_ret = (ret > (sil_supp_buff + s_sounlev_minvol)) ? (ret - sil_supp_buff) : s_sounlev_minvol;
		gLog.FormattedMessage("Have Sil Or Supp Sound Level %u", deal_ret);
#endif //VariousSoundLevelDebug
		if (!cur_baseproj) return (ret > (sil_supp_buff + s_sounlev_minvol)) ? (ret - sil_supp_buff) : s_sounlev_minvol;

		// check proj is subsonic
		INT32 subsonic_buff = (((cur_baseproj->speed) < SOUND_LEV_MNG.generic_subsonic_speed) * (SOUND_LEV_MNG.generic_subsonic_decrement));
		// check proj done

		// check proj when apply sil_supp_buff
		if (const auto& iter = SOUND_LEV_MNG_PJMAP.find(cur_baseproj->refID); iter != SOUND_LEV_MNG_PJMAP.end()) {
			subsonic_buff = iter->second;
		}
		// check proj when apply sil_supp_buff done

#if UsefulSoundLevelDebug
		auto deal_ret_2 = (ret > (sil_supp_buff + subsonic_buff + s_sounlev_minvol)) ? (ret - (sil_supp_buff + subsonic_buff)) : s_sounlev_minvol;
		gLog.FormattedMessage("subsonic_buff = %d, Have Sil Or Supp And Subsonic Proj Setting  %u", subsonic_buff, deal_ret_2);
#endif //VariousSoundLevelDebug

		return (ret > (sil_supp_buff + subsonic_buff + s_sounlev_minvol)) ? (ret - (sil_supp_buff + subsonic_buff)) : s_sounlev_minvol;
	}

	enum CreatureType : UInt8{
		NotCreature = 8,
		Animal = 0,
		Mutated_Animal = 1,
		Mutated_Insect = 2,
		Abomination = 3,	// Intellective
		Supermutant = 4,	// Intellective
		Feral_Ghoul = 5,
		Robot = 6,			// Intellective
		Giant = 7
	};

	enum Ret_CreatureType : UInt8 {
		Ret_Creature = 0,
		Ret_IntellectiveCreature = 1,
		Ret_NotCreature = 2
	};

	static CreatureType __forceinline GetCreatureType(const Actor* _actor) {
		if (const TESCreature* creature = (TESCreature*)(_actor->GetActorBase())) {
			if IS_ID(creature, TESCreature) {
				return (CreatureType)creature->type;
			}
		}
		return NotCreature;
	}

	static bool __forceinline IsIntellective(const Actor* _actor) {
		CreatureType c_type = GetCreatureType(_actor);
		return (c_type == NotCreature || c_type == Abomination || c_type == Supermutant || c_type == Robot);
	}

	static auto __forceinline PickCreatureDetectionDis(CreatureType c_type) {
		struct pick_ret{
			Ret_CreatureType is_creature = Ret_NotCreature;
			UINT32 detection_max_distance = 0;
		};
		
		switch (c_type)
		{
		case Abomination:return pick_ret{ Ret_IntellectiveCreature,g_distance_for_Abomination };
		case Supermutant:return pick_ret{ Ret_IntellectiveCreature,g_distance_for_Supermutant };
		case Robot:return pick_ret{ Ret_IntellectiveCreature,g_distance_for_Robot };
		case NotCreature: return pick_ret{ Ret_NotCreature,0};
		default:
			return pick_ret{ Ret_Creature,g_distance_for_creature };
		}
		return pick_ret{ Ret_NotCreature,0 };
	}

	static float __forceinline GetTotalUnpausedTime() {
		return CdeclCall<float>(0x435DD0);
	}

	static bool __forceinline InvisibilityOrChameleon(const Actor* _actor) {
		// check invisibility and chameleon
		return _actor->avOwner.GetActorValue(48) > 0 || _actor->avOwner.GetActorValueInt(49) > 0;
	}

	/*
	Add detection value to nearby actor
	if detected is playerref, will fill up radia_set for radiation detect
	*/

	static void __fastcall AddDetectionDataToNearby(Actor* detected, INT32 SoundLevel, 
													bool do_radia = false,
													std::set<radiation_data>* radia_set=nullptr) {
		static ProcessManager* procMngr = (ProcessManager*)0x11E0E80;
		bool detected_pc = detected->IsPlayer();
		if (detected->IsDeleted() || detected->GetDisabled() || detected->GetIsGhost()) return;
		if (detected->isTeammate && !IsFlagOn(ApplyToTeammate)) return;
		if (!detected->isTeammate && !detected_pc && IsFlagOn(PCOnly)) return;
		if (!IsIntellective(detected)) return;		// in case NPC go to fight non-intellective creature
		if (detected_pc && do_radia && !radia_set ) return;
		auto iter = procMngr->highActors.Head();
		if (!iter) return;
		
		do
		{
			if (Actor* _actor = iter->data) { 
				if (_actor->IsPlayer() || _actor->IsDeleted() || _actor->GetDisabled() || _actor->GetIsGhost()) continue;
				if (!IsIntellective(_actor) && !IsFlagOn(CreatureCanDetection)) continue; // not change non-intellective creature detection val

// distance check
				float actor_max_detection_dis = 0;
				float Dis = detected->GetDistance(_actor);
				if (const auto& pick_ret = PickCreatureDetectionDis(GetCreatureType(_actor)); pick_ret.is_creature != Ret_NotCreature) {
					actor_max_detection_dis = pick_ret.detection_max_distance;
					if (pick_ret.is_creature == Ret_IntellectiveCreature && !detected_pc)	// IntellectiveCreature will regard as npc
						actor_max_detection_dis = g_distance_for_npc;
				}
				else actor_max_detection_dis = (detected_pc ? g_distance_for_pc : g_distance_for_npc);
				if (Dis > actor_max_detection_dis) continue;
// distance check

				if (BaseProcess* bsProc = _actor->baseProcess; !bsProc->processLevel) {
					if (DetectionData* data = bsProc->GetDetectionData(detected, 0)) {
						float DisRatio = 1 - (Dis / actor_max_detection_dis);
						float Result = (SoundLevel * DisRatio);
						if (Result < s_sounlev_minvol) Result = s_sounlev_minvol;
						Result *= SOUND_LEV_MNG.generic_detection_scale;
						data->detectedLocation.x = detected->posX;
						data->detectedLocation.y = detected->posY;
						data->detectedLocation.z = detected->posZ;
						if (Result > data->detectionValue) {
							if (do_radia) {	// for radiation
								radia_set->emplace(radiation_data{ Result,_actor });
							}
							data->detectionValue = static_cast<SInt32>(Result);
							if (!data->inLOS || InvisibilityOrChameleon(_actor)) data->detectionLevel = 2;	// seems not works very well
							else data->detectionLevel = 3;
							data->fTimeStamp = GetTotalUnpausedTime();										// maybe useful?
						}
					}
				}
			}
		} while (iter = iter->next);
	}

	static __forceinline void SetDetectionOfPC(Actor* _actor,Actor* detect,float new_detection_val) {
		if (BaseProcess* bsProc = _actor->baseProcess; !bsProc->processLevel)
			if (DetectionData* data = bsProc->GetDetectionData(detect, 0))
				data->detectionValue = new_detection_val;
	}

	static void DetectionRadiation(std::set<radiation_data>* radia_set,Actor* detected) {
		if (radia_set->empty()) return;
		radiation_data max_info{ -1,nullptr };
		if (const auto& first_data = *radia_set->begin(); first_data.centre) {
			max_info.detection_val_to_PC = first_data.detection_val_to_PC;
			max_info.centre = first_data.centre;
			//gLog.FormattedMessage("cur max radiatie DV %.2f",max_info.detection_val_to_PC);
		}
		else return;
		float radia_detection_dis = 0;
		for (const auto& radia_data : (*radia_set) ){
			if (radia_data.centre == max_info.centre) continue;
			// distance check
			
			if (const auto& pick_ret = PickCreatureDetectionDis(GetCreatureType(radia_data.centre)); pick_ret.is_creature)
				radia_detection_dis = pick_ret.detection_max_distance;
			else radia_detection_dis = SOUND_LEV_MNG.generic_radiation_distance;
			
			if (max_info.detection_val_to_PC > radia_data.detection_val_to_PC &&
				max_info.InRange(radia_data.centre, radia_detection_dis)) {
				//gLog.Message("Do Radiation");
				SetDetectionOfPC(radia_data.centre, detected, max_info.detection_val_to_PC);
			}	
			else {
				max_info.detection_val_to_PC = radia_data.detection_val_to_PC;
				max_info.centre = radia_data.centre;
			}
		}


	}

	static INT32 __fastcall GetSoundLevelAmountFuncCaller_8BAE42(TESObjectWEAP* _this, Actor* _actor, UINT32 has_sil, UINT32 has_supp) {
		if (_this && IS_TYPE(_this,TESObjectWEAP)) {
			if (_this->attackDmg.damage == 0 ||  _this->equipType.equipType == BGSEquipType::EquipTypes::kEqpType_Mine) {
				return ThisStdCall<INT32>(SavedGetSoundLevelAmountFunc, _this, has_sil, has_supp);
			}
		}
		
		INT32 soundlevel = GetSoundLevelAmountFunc_8BAE42(_this, _actor, has_sil, has_supp);
		if (!_actor || !IS_ACTOR(_actor)) return soundlevel;
		
		HighProcess* hgProc = (HighProcess*)_actor->baseProcess;
		if (!hgProc || hgProc->processLevel) return soundlevel;
		if (_actor->IsPlayer()) {
			if (IsFlagOn(RadiationDetection)){
				std::set<radiation_data> radia_set{};
				AddDetectionDataToNearby(_actor, soundlevel, true, &radia_set);
				DetectionRadiation(&radia_set, _actor);// only for player
			}
			else AddDetectionDataToNearby(_actor, soundlevel); 
		}
		else AddDetectionDataToNearby(_actor, soundlevel); 
		
		CreateDetectionEvent(hgProc, _actor, _actor->posX, _actor->posY, _actor->posZ, soundlevel, 3, _actor);
		return soundlevel;
	}

	static __declspec(naked) void Caller_8BAE42() {
		__asm {
			mov edx, [ebp - 0xA0]	// actor
			jmp GetSoundLevelAmountFuncCaller_8BAE42
		}
	}

	static inline bool ReadCustomConfig() {
		gLog.Message("ReadCustomConfig");
		fs::path custom_config_dir = fs::current_path();
		custom_config_dir += R"(\Data\NVSE\Plugins\UsefulSoundLevelConfig\custom\)";
		if (!fs::exists(custom_config_dir)) {
			gLog.Message("CustomConfig path not exist");
			return false;
		}

		INIReader _ini{ "not_now"sv };
		std::string l_modname = "";
		for (auto const& dir_entry : std::filesystem::directory_iterator{ custom_config_dir }) {
			if (dir_entry.path().extension() != ".ini") continue;			// *.ini
			l_modname = dir_entry.path().stem().string();
			UINT32 mod_index = GetU32ModIndex(l_modname);
			gLog.FormattedMessage("Reading %s, mod index %x", l_modname.c_str(), mod_index);
			if (mod_index == 0xff000000) continue;

			auto ret = _ini.SetCurrentPath(dir_entry.path());
			if (!ret.has_value()) {
				gLog.FormattedMessage("Failed to set ini path for custom config reading.Error msg: %s", ret.error().message());
				continue;
			}
			ret = _ini.ConstructSectionMap();
			if (!ret.has_value()) {
				gLog.FormattedMessage("Failed to construct sectionmap when reading custom config.Error msg: %s", ret.error().message());
				continue;
			}
			if (const auto* pj_kv_map = _ini.GetSectionKeyValMapCst("ProjectileOverride")) {
				for (auto iter = pj_kv_map->begin(); iter != pj_kv_map->end(); iter++) {
					if (UINT32 formid = FormExist(mod_index, iter->first); formid > 0) {
						SOUND_LEV_MNG_PJMAP.try_emplace(formid, static_cast<UINT8>(std::stoi(iter->second)));
					}
				}
			}
			if (const auto* wp_kv_map = _ini.GetSectionKeyValMapCst("WeaponOverride")) {
				for (auto iter = wp_kv_map->begin(); iter != wp_kv_map->end(); iter++) {
					if (UINT32 formid = FormExist(mod_index, iter->first); formid > 0) {
						SOUND_LEV_MNG_WPMAP.try_emplace(formid, static_cast<UINT8>(std::stoi(iter->second)));
					}
				}
			}

		}
		return true;
	}

	static inline bool ReadGenericConfig() {
		gLog.Message("ReadGenericConfig");
		fs::path config_root_path = fs::current_path();
		config_root_path += R"(\Data\NVSE\Plugins\UsefulSoundLevelConfig\)";
		if (!fs::exists(config_root_path)) {
			gLog.Message("ReadGenericConfig path not exist");
			return false;
		}

		INIReader _ini{ config_root_path };

		auto ret = _ini.SetCurrentINIFileName("useful_sound_level.ini");
		if (!ret.has_value()) {
			gLog.FormattedMessage("Failed to set generic config filename : %s", ret.error().message());
			return false;
		}
		ret = _ini.ConstructSectionMap();
		if (!ret.has_value()) {
			gLog.FormattedMessage("Failed to construct section map : %s", ret.error().message());
			return false;
		}

		// init the generic config
		std::string raw_type_val = "";
		raw_type_val = _ini.GetRawTypeVal("General", "uMinSoundLevelVolume");
		s_sounlev_minvol = raw_type_val.empty() ? 10 : static_cast<UINT8>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "dVanillaMaxDetectionDistance");
		g_VanillaDetectionDistance = raw_type_val.empty() ? 8192 : (std::stod(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "uDistanceForPC");
		g_distance_for_pc = raw_type_val.empty() ? 8192 : static_cast<UINT32>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "uDistanceForNPC");
		g_distance_for_npc = raw_type_val.empty() ? 4096 : static_cast<UINT32>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "CreatureDetectionDistance");
		g_distance_for_creature = raw_type_val.empty() ? 1280 : static_cast<UINT32>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "AbominationDetectionDistance");
		g_distance_for_Abomination = raw_type_val.empty() ? 2048 : static_cast<UINT32>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "SupermutantDetectionDistance");
		g_distance_for_Supermutant = raw_type_val.empty() ? 12000 : static_cast<UINT32>(std::stoi(raw_type_val));
		
		raw_type_val = _ini.GetRawTypeVal("General", "RobotDetectionDistance");
		g_distance_for_Robot = raw_type_val.empty() ? 12000 : static_cast<UINT32>(std::stoi(raw_type_val));

		UINT8 temp_flag = 0;
		raw_type_val = _ini.GetRawTypeVal("General", "uMode");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag |= RealisticMode;

		raw_type_val = _ini.GetRawTypeVal("General", "uPlayerOnly");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag |= PCOnly;

		raw_type_val = _ini.GetRawTypeVal("General", "uTeammate");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag |= ApplyToTeammate;

		raw_type_val = _ini.GetRawTypeVal("General", "uCreature");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag |= CreatureCanDetection;

		raw_type_val = _ini.GetRawTypeVal("General", "uDisableVanillaDetectionEventOfImpact");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag |= DisableDetectionEvent;

		raw_type_val = _ini.GetRawTypeVal("General", "uDetectionRadiate");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag |= RadiationDetection;

		raw_type_val = _ini.GetRawTypeVal("General", "uGeneralSoundLevelVolume");
		SOUND_LEV_MNG.generic_sound_volume = raw_type_val.empty() ? 100 : static_cast<UINT32>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "fRadiateDetectionDistance");
		SOUND_LEV_MNG.generic_radiation_distance = raw_type_val.empty() ? 2048 : (std::stof(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "fDetectionValueScale");
		SOUND_LEV_MNG.generic_detection_scale = raw_type_val.empty() ? 1.0 : (std::stof(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "uGeneralSilenceDecrement");
		SOUND_LEV_MNG.generic_silence_decrement = raw_type_val.empty() ? 15 : static_cast<UINT8>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "uGeneralSuppressorDecrement");
		SOUND_LEV_MNG.generic_suppressor_decrement = raw_type_val.empty() ? 5 : static_cast<UINT8>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "uSubsonicSpeedThreshold");
		SOUND_LEV_MNG.generic_subsonic_speed = raw_type_val.empty() ? 32000 : static_cast<UINT32>(std::stoi(raw_type_val));

		raw_type_val = _ini.GetRawTypeVal("General", "uSubsonicDecrement");
		SOUND_LEV_MNG.generic_subsonic_decrement = raw_type_val.empty() ? 60 : static_cast<UINT8>(std::stoi(raw_type_val));

		return true;
	}

	static inline bool ReadConfig() {
		if (!ReadGenericConfig()) {
			gLog.FormattedMessage("Read Generic Config Failed,Plugin Feature Disable");
			return false;
		}
		if (!ReadCustomConfig()) {
			gLog.FormattedMessage("Failed To Read Custom Config");
		}
		return true;
	}

	static void SafeWriteBuf(UInt32 addr, const char* data, UInt32 len)
	{
		UInt32	oldProtect;

		VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
		memcpy((void*)addr, data, len);
		VirtualProtect((void*)addr, len, oldProtect, &oldProtect);
	}

	__declspec(noinline) void __stdcall SafeWriteDouble(double* double_addr, double double_data)
	{
		UInt32 oldProtect;
		VirtualProtect((void*)double_addr, 8, PAGE_EXECUTE_READWRITE, &oldProtect);
		*double_addr = double_data;
		VirtualProtect((void*)double_addr, 8, oldProtect, &oldProtect);
	}

	static inline void InstallHook()
	{
		SavedGetSoundLevelAmountFunc = GetRelJumpAddr(0x8BAE42);
		if (ReadConfig()) {
#if UsefulSoundLevelDebug
			PrintSoundLevelMng();
			PrintSoundLevelMngPJ();
			PrintSoundLevelMngWp();
#endif
			if (IsFlagOn(DisableDetectionEvent))SafeWriteBuf(0x9C2B64, "\x90\x90", 2);		// Cancel CreateDetectionEvent Of Impact
			WriteRelCall(0x8BAE42, UInt32(Caller_8BAE42));

			SafeWriteDouble((double*)0x1084D28, g_VanillaDetectionDistance);
		}

	}


};



// This is a message handler for nvse events
// With this, plugins can listen to messages such as whenever the game loads
void MessageHandler(NVSEMessagingInterface::Message* msg)
{
	switch (msg->type)
	{
	case NVSEMessagingInterface::kMessage_DeferredInit:
		SoundLevelMatter::InstallHook();
		break;
	}
}

bool NVSEPlugin_Load(NVSEInterface* nvse)
{
	g_pluginHandle = nvse->GetPluginHandle();

	// save the NVSE interface in case we need it later
	g_nvseInterface = nvse;

	// register to receive messages from NVSE

	if (!nvse->isEditor)
	{
		g_messagingInterface = static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(kInterface_Messaging));
		g_messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler);
	}
	return true;
}