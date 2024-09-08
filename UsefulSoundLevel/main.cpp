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

/* ¡ý Copied From JIP ¡ý  */
#define ADDR_ReturnTrue	0x8D0360
#define IS_ACTOR(form) ((*(UInt32**)form)[0x100 >> 2] == ADDR_ReturnTrue)
/* ¡ü Copied From JIP ¡ü */

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
	LoadedModFinder_NoExt(const char* str) : m_stringToFind(str){ }

	bool Accept(ModInfo* modInfo){
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
UINT32 static inline GetU32ModIndex(const std::string& mod_name){
	const auto* mod = LookupModByName_NoExt(mod_name.c_str());
	if (!mod || NULL == mod) {
		gLog.FormattedMessage("mod %s not found",mod_name.c_str());
		return 0xff000000; 
	}
	
	return (mod->modIndex << 24);
}

// return form id when formexist,or return 0
UINT32 static inline FormExist(const UINT32& u32_mod_index,const std::string& str_formid) {
	int formid = -1;
	try {
		formid = std::stoi(str_formid, 0, 16);
	}
	catch (...) {
		gLog.FormattedMessage("%s is not valid formid",str_formid);
		return 0;
	}
	if (!formid || formid > 0x00ffffff) {
		gLog.FormattedMessage("%s is not valid formid", str_formid);
		return 0;
	}
	formid = u32_mod_index + formid;
	
	const auto* form = LookupFormByID(formid);
	if (!form){
		gLog.FormattedMessage("form %x is not found", formid);
		return 0;
	}
	return formid;
}


namespace SoundLevelMatter {
	using namespace roughinireader;
	
	static UINT32 SavedGetSoundLevelAmountFunc;
	static UINT8 g_my_detection_flag = 0;	// |0 - mode |0 0 0 | 0 - DisableDetectionEvent | 0 - work for creature | 0 - work for teammate | 0 - only for pc |
	static UINT32 g_distance_for_pc = 8192;
	static UINT32 g_distance_for_npc = 4096;
	static double g_VanillaDetectionDistance = 8192;
	static UINT8 s_sounlev_minvol = 10;
	struct SoundLevelMng {
//using weap_sound_map = std::map<UINT32, std::array<UINT8, 4>>;
using weap_sound_map = std::map<UINT32, UINT8>;
using proj_sound_map = std::map<UINT32, UINT8>;
		UINT32 generic_subsonic_speed;
		UINT8 generic_sound_volume;
		UINT8 generic_silence_decrement;
		UINT8 generic_suppressor_decrement;
		UINT8 generic_subsonic_decrement;
		std::unique_ptr<weap_sound_map> wp_sound_map;
		std::unique_ptr<proj_sound_map> pj_sound_map;
		SoundLevelMng(SoundLevelMng &&) = delete;
	private:
		SoundLevelMng(){
			generic_subsonic_speed = 0;
			generic_sound_volume = 0;
			generic_silence_decrement = 0;
			generic_suppressor_decrement = 0;
			generic_subsonic_decrement = 0;
			wp_sound_map = std::make_unique<weap_sound_map>();
			pj_sound_map = std::make_unique<proj_sound_map>();
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
#define SOUND_LEV_MNG_PJMAP SoundLevelMng::GetSingleton().pj_sound_map.get()
#define SOUND_LEV_MNG_WPMAP SoundLevelMng::GetSingleton().wp_sound_map.get()

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
		auto* pj_kv_map = SOUND_LEV_MNG.pj_sound_map.get();
		for (auto iter = pj_kv_map->begin(); iter != pj_kv_map->end(); iter++) {
			gLog.FormattedMessage("PJ Formid %x, Value %d",iter->first,iter->second);
		}
	}

	static inline void PrintSoundLevelMngWp() {
		auto* wp_kv_map = SOUND_LEV_MNG.wp_sound_map.get();
		for (auto iter = wp_kv_map->begin(); iter != wp_kv_map->end(); iter++) {
			gLog.FormattedMessage("Wp Formid %x, Value %u", iter->first, iter->second);
		}
	}
#define PCOnly 0b00000001
#define ApplyToTeammate 0b00000010
#define ApplyToCreature 0b00000100
#define DisableDetectionEvent 0b00001000
#define RealisticMode 0b10000000

	// return true when the flag is on
	static __forceinline bool CheckDetectionFlag(UINT8 _flag) {
		return (g_my_detection_flag & _flag) != 0;
	}


	static __forceinline void CreateDetectionEvent(BaseProcess* _this,Actor* _actor,float pX,float pY,float pZ,
													UINT32 soundLevel,UINT32 eventtype,TESObjectREFR* locationRef) {
		//CreateDetectionEvent
		ThisStdCall(0x903E40,_this,_actor,pX,pY,pZ,soundLevel,eventtype,locationRef);
	}


	static __forceinline BGSProjectile* GetBaseProjOfCurrentEqAmmo(TESObjectWEAP* _this,Actor* _actor)
	{
		return ThisStdCall<BGSProjectile*>(0x525A90,_this,_actor);
	}

	static INT32 __fastcall GetSoundLevelAmountFunc_8BAE42(TESObjectWEAP* _this, Actor* _actor, UINT32 has_sil,UINT32 has_supp)
	{
		INT32 ret = ThisStdCall<INT32>(SavedGetSoundLevelAmountFunc, _this, has_sil, has_supp);	// In fact I don't care what you return :)
		if (!_actor || !CheckDetectionFlag(RealisticMode)) return ret;
		
		ret = SOUND_LEV_MNG.generic_sound_volume;

		INT32 sil_supp_buff = ((has_supp > 0) * (SOUND_LEV_MNG.generic_suppressor_decrement));
		if( has_sil > 0) sil_supp_buff = ((has_sil > 0) * (SOUND_LEV_MNG.generic_silence_decrement));

// check weapon at first
		if (const auto& iter = SOUND_LEV_MNG_WPMAP->find(_this->refID); iter != SOUND_LEV_MNG_WPMAP->end()) {
			sil_supp_buff = iter->second;
		}
// check weapon done

#if UsefulSoundLevelDebug
		gLog.FormattedMessage("New Sound Level %u",ret);
#endif	//VariousSoundLevelDebug

		if (sil_supp_buff == 0) return ret;								// No Sil and Supp, return
		auto cur_baseproj = GetBaseProjOfCurrentEqAmmo(_this, _actor);

#if UsefulSoundLevelDebug
		auto deal_ret = (ret > (sil_supp_buff + s_sounlev_minvol)) ? (ret - sil_supp_buff) : s_sounlev_minvol;
		gLog.FormattedMessage("Have Sil Or Supp Sound Level %u", deal_ret);
#endif //VariousSoundLevelDebug
		if (!cur_baseproj) return (ret > (sil_supp_buff + s_sounlev_minvol) ) ? (ret - sil_supp_buff) : s_sounlev_minvol;

// check proj is subsonic
		INT32 subsonic_buff = ( ((cur_baseproj->speed) < SOUND_LEV_MNG.generic_subsonic_speed) * (SOUND_LEV_MNG.generic_subsonic_decrement) );
// check proj done

// check proj when apply sil_supp_buff
		if (const auto& iter = SOUND_LEV_MNG_PJMAP->find(cur_baseproj->refID); iter != SOUND_LEV_MNG_PJMAP->end()) {
			subsonic_buff = iter->second;
		}
// check proj when apply sil_supp_buff done
		
#if UsefulSoundLevelDebug
		auto deal_ret_2 = (ret > (sil_supp_buff + subsonic_buff + s_sounlev_minvol)) ? (ret - (sil_supp_buff + subsonic_buff)) : s_sounlev_minvol;
		gLog.FormattedMessage("subsonic_buff = %d, Have Sil Or Supp And Subsonic Proj Setting  %u", subsonic_buff, deal_ret_2);
#endif //VariousSoundLevelDebug
		
		return (ret > (sil_supp_buff + subsonic_buff + s_sounlev_minvol)) ? (ret - (sil_supp_buff + subsonic_buff) ) : s_sounlev_minvol;
}

	static bool __forceinline IsCreatureButNotIntellective(const Actor* _actor) {
		if (const TESCreature* creature = (TESCreature*)(_actor->GetActorBase()) ){
			if IS_ID(creature,TESCreature){
				// Not Abomination,Supermutant and Robot
				if (creature->type == 3 || creature->type == 4 || creature->type == 6) return false;
				return true;
			}
		}
		return false;
	}

	static float __forceinline GetTotalUnpausedTime() {
		return CdeclCall<float>(0x435DD0);
	}

	static bool __forceinline InvisibilityOrChameleon(const Actor* _actor) {
		// check invisibility and chameleon
		return _actor->avOwner.GetActorValue(48)>0 || _actor->avOwner.GetActorValueInt(49)>0;
	}

	static void __fastcall AddDetectionDataToNearby(Actor* detected,INT32 SoundLevel) {
		static ProcessManager* procMngr =  (ProcessManager*)0x11E0E80;
		if (detected->IsDeleted() || detected->GetDisabled() || detected->GetIsGhost() || detected->GetAlpha() == 0) return;
		if (detected->isTeammate && !CheckDetectionFlag(ApplyToTeammate)) return;
		if (!detected->isTeammate && !detected->IsPlayerRef() && CheckDetectionFlag(PCOnly)) return;
		if (IsCreatureButNotIntellective(detected)) return;

		auto iter = procMngr->highActors.Head();
		if (!iter) return;
		float DisComp = detected->IsPlayerRef() ? g_distance_for_pc : g_distance_for_npc;
		do 
		{
			if (Actor* _actor = iter->data){
				if (_actor->IsDeleted() || _actor->GetDisabled() || _actor->GetIsGhost() || _actor->GetAlpha() == 0) continue;
				
				if (IsCreatureButNotIntellective(_actor) && !CheckDetectionFlag(ApplyToCreature)) continue;

				if (BaseProcess* bsProc = _actor->baseProcess; !bsProc->processLevel) {
					if (DetectionData* data = bsProc->GetDetectionData(detected, 0)) {
						if (float Dis = detected->GetDistance(_actor) ; Dis < DisComp){
							float DisRatio = 1 - (Dis / DisComp);
							float Result = (SoundLevel * DisRatio);
							if (Result < s_sounlev_minvol) Result = s_sounlev_minvol;
							data->detectedLocation.x = detected->posX;
							data->detectedLocation.y = detected->posY;
							data->detectedLocation.z = detected->posZ;
							//gLog.FormattedMessage("current actor %s,detect %s", _actor->GetFullName()->name.m_data, detected->GetFullName()->name.m_data);
							//gLog.FormattedMessage("Before DetectionData: detection level %u,value %d, should be %f", data->detectionLevel, data->detectionValue, Result);
							if (Result > data->detectionValue) {			
								data->detectionValue = static_cast<SInt32>(Result);
								if (!data->inLOS || InvisibilityOrChameleon(_actor) ) data->detectionLevel = 2;	// seems not works very well
								else data->detectionLevel = 3;
								data->fTimeStamp = GetTotalUnpausedTime();										// maybe useful?
							}
							//gLog.FormattedMessage("After DetectionData: detection level %u,value %d, should be %f", data->detectionLevel, data->detectionValue, Result);
						}
						
					}
				}
					
			}
		} while (iter = iter->next);
	}

	static INT32 __fastcall GetSoundLevelAmountFuncCaller_8BAE42(TESObjectWEAP* _this, Actor* _actor, UINT32 has_sil, UINT32 has_supp) {
		INT32 soundlevel = GetSoundLevelAmountFunc_8BAE42(_this, _actor, has_sil, has_supp);
		if (!_actor) return soundlevel;

		HighProcess* hgProc = (HighProcess*)_actor->baseProcess;
		if (!hgProc || hgProc->processLevel) return soundlevel;
		AddDetectionDataToNearby(_actor, soundlevel);
		CreateDetectionEvent(hgProc, _actor,_actor->posX,_actor->posY,_actor->posZ,soundlevel,3, _actor);
		return soundlevel;
	}

	static __declspec(naked) void Caller_8BAE42() {
		__asm {
			mov edx,[ebp - 0xA0]	// actor
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
			gLog.FormattedMessage("Reading %s, mod index %x", l_modname.c_str(),mod_index);
			if (mod_index == 0xff000000) continue;

			auto ret = _ini.SetCurrentPath(dir_entry.path());
			if (!ret.has_value()){
				gLog.FormattedMessage("Failed to set ini path for custom config reading.Error msg: %s",ret.error().message());
				continue;
			}
			ret = _ini.ConstructSectionMap();
			if (!ret.has_value()) {
				gLog.FormattedMessage("Failed to construct sectionmap when reading custom config.Error msg: %s", ret.error().message());
				continue;
			}
			if (const auto* pj_kv_map = _ini.GetSectionKeyValMapCst("ProjectileOverride")){
				for (auto iter = pj_kv_map->begin(); iter != pj_kv_map->end(); iter++) {
					if (UINT32 formid = FormExist(mod_index, iter->first);formid > 0) {
						SOUND_LEV_MNG.pj_sound_map->try_emplace(formid, static_cast<UINT8>(std::stoi(iter->second)));
					}
				}
			}
			if (const auto* wp_kv_map = _ini.GetSectionKeyValMapCst("WeaponOverride")) {
				for (auto iter = wp_kv_map->begin(); iter != wp_kv_map->end(); iter++) {
					if (UINT32 formid = FormExist(mod_index, iter->first); formid > 0) {
						SOUND_LEV_MNG.wp_sound_map->try_emplace(formid, static_cast<UINT8>(std::stoi(iter->second)));
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
		if (!ret.has_value()){
			gLog.FormattedMessage("Failed to set generic config filename : %s",ret.error().message());
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
		
		UINT8 temp_flag = 0;
		raw_type_val = _ini.GetRawTypeVal("General", "uMode");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag += RealisticMode;

		raw_type_val = _ini.GetRawTypeVal("General", "uPlayerOnly");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag += PCOnly;

		raw_type_val = _ini.GetRawTypeVal("General", "uTeammate");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag += ApplyToTeammate;

		raw_type_val = _ini.GetRawTypeVal("General", "uCreature");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag += ApplyToCreature;

		raw_type_val = _ini.GetRawTypeVal("General", "uDisableVanillaDetectionEventOfImpact");
		temp_flag = raw_type_val.empty() ? 0 : static_cast<UINT8>(std::stoi(raw_type_val));
		if (temp_flag > 0) g_my_detection_flag += DisableDetectionEvent;

		raw_type_val = _ini.GetRawTypeVal("General", "uGeneralSoundLevelVolume");
		SOUND_LEV_MNG.generic_sound_volume = raw_type_val.empty() ? 100 : static_cast<UINT32>(std::stoi(raw_type_val));
		
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
		if (!ReadGenericConfig()){
			gLog.FormattedMessage("Read Generic Config Failed,Plugin Feature Disable");
			return false;
		}
		if (!ReadCustomConfig()){
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
			if(CheckDetectionFlag(DisableDetectionEvent))SafeWriteBuf(0x9C2B64, "\x90\x90", 2);		// Cancel CreateDetectionEvent Of Impact
			WriteRelCall(0x8BAE42, UInt32(Caller_8BAE42));
			
			SafeWriteDouble((double*)0x1084D28,g_VanillaDetectionDistance);
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
