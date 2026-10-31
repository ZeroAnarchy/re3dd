#pragma once
// Bounded, opt-in diagnostics only. The hook supplies a coherent native snapshot;
// write after the sequence, never modify native pose, animation or saved progress.
static std::string recordModelPoseSequence(const re3bridge::ActorContext& context,const char* mode,const re3shoulder::Factors* shoulder=nullptr){
    static std::string label;static unsigned target=0,stride=0,count=0;static uint32_t previous=0;
    static std::vector<std::string> rows;
    if(!GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str()))return {};
    if(!modelSequenceRequest.empty()){
        label=modelSequenceRequest;modelSequenceRequest.clear();target=modelSequenceFrames;stride=modelSequenceCaptureStride;count=0;previous=0;rows.clear();rows.reserve(target);
        log("MODEL_SEQUENCE begin %s frames=%u capturesEvery=%u",label.c_str(),target,stride);
    }
    if(label.empty()||count>=target||context.serial==previous)return {};
    previous=context.serial;
    std::ostringstream out;out<<std::setprecision(9);
    auto matrix=[&](const re3bridge::Matrix32& m){out<<"[";for(unsigned i=0;i<12;i++){if(i)out<<",";if(i%4==3)out<<m.translation[i/4];else out<<m.rotation[(i/4)*3+i%4]/4096.f;}out<<"]";};
    auto actor=static_cast<const uint8_t*>(context.actor);
    out<<"{\"index\":"<<count<<",\"serial\":"<<context.serial<<",\"time_ms\":"<<modelClockMs()<<",\"skin_mode\":\""<<mode<<"\",\"scene\":\""<<currentScene<<"\",\"weapon_id\":"<<unsigned(context.weaponId)<<",\"actor_flags\":"<<context.flags<<",\"routine\":[";
    for(unsigned i=4;i<8;i++){if(i!=4)out<<",";out<<unsigned(actor[i]);}out<<"],\"animation\":[";
    for(unsigned i:{0xc8u,0xc9u,0xcau,0x1a0u,0x1a1u,0x1a2u}){if(i!=0xc8u)out<<",";out<<unsigned(actor[i]);}out<<"],\"actor_xyz\":[";
    for(unsigned i=0;i<3;i++){if(i)out<<",";out<<re3bridge::detail::Read<int32_t>(actor,0x34+i*4);}out<<"],\"actor_angles\":[";
    for(unsigned i=0;i<3;i++){if(i)out<<",";out<<re3bridge::detail::Read<int16_t>(actor,0x6c+i*2);}out<<"],\"mapped_input\":"<<re3bridge::detail::Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c864))<<",\"camera\":";matrix(context.camera);
    out<<",\"world_bones\":[";for(unsigned i=0;i<15;i++){if(i)out<<",";matrix(context.bones[i].world);}out<<"],\"part_flags\":[";
    for(unsigned i=0;i<15;i++){if(i)out<<",";out<<context.bones[i].flags;}out<<"],\"part_render_attributes\":[";
    for(unsigned i=0;i<15;i++){if(i)out<<",";out<<context.bones[i].renderAttributes;}out<<"],\"projection_h\":"<<re3bridge::detail::Read<uint16_t>(context.gte.data(),0x78)<<",\"zsf3\":"<<re3bridge::detail::Read<int16_t>(context.gte.data(),0x80);
    if(shoulder)out<<",\"shoulder_corrective_factors\":["<<shoulder->side[0]<<","<<shoulder->side[1]<<"],\"raised_arm_corrective_factors\":["<<shoulder->raised[0]<<","<<shoulder->raised[1]<<"]";
    out<<"}";
    rows.push_back(out.str());std::string capture;if(stride&&count%stride==0)capture="SEQ-"+label+"-"+std::to_string(count);++count;
    if(count==target){CreateDirectoryA((root+"/pose-sequences").c_str(),nullptr);std::ofstream file(root+"/pose-sequences/"+label+".json");file<<"{\"schema\":\"native-pose-sequence-v1\",\"coordinate_space\":\"world Y-down row-major3x4 column-vector\",\"capture_stride\":"<<stride<<",\"frames\":[";
        for(size_t i=0;i<rows.size();i++){if(i)file<<",";file<<rows[i];}file<<"]}\n";log("MODEL_SEQUENCE complete %s count=%u",label.c_str(),count);label.clear();rows.clear();}
    return capture;
}
