// Mesh include after viewer globals; all GPU objects persist through drain.
static bool mesh_active=false;
static native_mesh::Planner mesh_plan;
static ntr::gx_geometry::Resources mesh_resources;
static ntr::HostVector<V3dsBatch> mesh_batches;
static ntr::HostVector<native_mesh::Position> mesh_position_snapshot;
static ntr::HostVector<uint16_t> mesh_index_snapshot;
static DVLB_s* mesh_dvlb=nullptr;

static C3D_AttrInfo mesh_attributes;
static native_mesh::Position* mesh_positions=nullptr;
static native_mesh::Attribute* mesh_dynamic=nullptr;
static uint16_t* mesh_indices=nullptr;
static const native_mesh::Packet* mesh_source=nullptr;
// Replaced only after GPU completion. Owns topology, matrices and texture leases
// for every centre/eye draw and retained HELD presentation.
static std::shared_ptr<const prepared_mesh::Frame> mesh_frame;
static uint64_t mesh_generation=UINT64_MAX;
static int mesh_u_fix,mesh_u_stereo,mesh_u_objects,mesh_u_texdims;
// gpu-geometry-ingame GpuLight: DS lighting uniforms of unified.v.pica.
static int mesh_u_lbase=-1,mesh_u_ldir=-1,mesh_u_ldif=-1,mesh_u_lspe=-1,mesh_b_light[4]={-1,-1,-1,-1};
static uint64_t mesh_env_uploads=0;
struct MeshStats {uint64_t frames=0,builds=0,reuses=0,static_hits=0,static_misses=0,position_bytes=0,index_bytes=0,dynamic_bytes=0,uniform_vectors=0,draws=0,runs=0,vertices=0,triangles=0,matrices=0;double drain_ms=0,prepare_ms=0,upload_ms=0;};
static MeshStats mesh_stats;
static bool mesh_init(){
 if(mesh_dvlb)return mesh_positions&&mesh_dynamic&&mesh_indices;
 mesh_dvlb=DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<unsigned char*>(native_mesh_shader)),sizeof(native_mesh_shader));
 if(!mesh_dvlb)return false;
 shaderProgramInit(&mesh_program);shaderProgramSetVsh(&mesh_program,&mesh_dvlb->DVLE[0]);
 auto* active=unified_shader_enabled()?g_prog.vertexShader:mesh_program.vertexShader;
 mesh_u_fix=shaderInstanceGetUniformLocation(active,"fix");
 mesh_u_stereo=shaderInstanceGetUniformLocation(active,"stereo");
 mesh_u_objects=shaderInstanceGetUniformLocation(active,"object");
 mesh_u_texdims=shaderInstanceGetUniformLocation(active,"texdims");
 if(unified_shader_enabled()){
  mesh_u_lbase=shaderInstanceGetUniformLocation(active,"lbase");mesh_u_ldir=shaderInstanceGetUniformLocation(active,"ldir");
  mesh_u_ldif=shaderInstanceGetUniformLocation(active,"ldif");mesh_u_lspe=shaderInstanceGetUniformLocation(active,"lspe");
  for(unsigned i=1;i<4;++i){char name[8]={'l','i','g','h','t',char('0'+i),0};mesh_b_light[i]=shaderInstanceGetUniformLocation(active,name);}
 }
 if(mesh_u_fix!=g_u_fix||mesh_u_stereo!=g_u_stereo||mesh_u_objects<0||mesh_u_objects+64>C3D_FVUNIF_COUNT||mesh_u_texdims>=C3D_FVUNIF_COUNT)return false;
 AttrInfo_Init(&mesh_attributes);AttrInfo_AddLoader(&mesh_attributes,0,GPU_SHORT,3);
 AttrInfo_AddLoader(&mesh_attributes,1,GPU_UNSIGNED_BYTE,4);AttrInfo_AddLoader(&mesh_attributes,2,GPU_FLOAT,2);AttrInfo_AddLoader(&mesh_attributes,3,GPU_UNSIGNED_BYTE,1);
 mesh_positions=static_cast<native_mesh::Position*>(linearAlloc(32768*sizeof(native_mesh::Position)));
 mesh_dynamic=static_cast<native_mesh::Attribute*>(linearAlloc(32768*sizeof(native_mesh::Attribute)));
 mesh_indices=static_cast<uint16_t*>(linearAlloc(32768*sizeof(uint16_t)));
 std::printf("Native mesh init uniforms=%d/%d/%d/%d variableFloat4=70 shaderConstantFloat4=4 totalFloat4=74 palette=16 linearBytes=%u remainingVRAM=%u\n",mesh_u_fix,mesh_u_stereo,mesh_u_objects,mesh_u_texdims,unsigned(32768*(sizeof(native_mesh::Position)+sizeof(native_mesh::Attribute)+2)),unsigned(vramSpaceFree()));
 return mesh_positions&&mesh_dynamic&&mesh_indices;
}
// DS lighting state -> unified.v.pica uniforms (gx_gpu_backend.inc upload_env):
// enabled lights compacted, colours in DS 5-bit units scaled for d/shine in
// 1/512 units, ambient and emission folded into lbase.
static void mesh_upload_env(const ntr::gx_gpu::Env& e){
 C3D_FVec* base=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,mesh_u_lbase,1);
 C3D_FVec* ldir=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,mesh_u_ldir,4);
 C3D_FVec* ldif=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,mesh_u_ldif,4);
 C3D_FVec* lspe=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,mesh_u_lspe,4);
 for(unsigned i=0;i<4;++i){ldir[i]=FVec4_New(0,0,0,0);ldif[i]=ldir[i];lspe[i]=ldir[i];}
 unsigned count=0;int sum[3]={e.emi[0]*32,e.emi[1]*32,e.emi[2]*32};
 for(unsigned i=0;i<4;++i){
  if(!((e.mask>>i)&1))continue;
  const auto& L=e.light[i];const int lc[3]={L.r,L.g,L.b};
  for(unsigned k=0;k<3;++k)sum[k]+=e.amb[k]*lc[k];
  ldir[count]=FVec4_New(L.dx,L.dy,L.dz,float(L.recip));
  ldif[count]=FVec4_New(e.dif[0]*lc[0]*(1.f/16384.f),e.dif[1]*lc[1]*(1.f/16384.f),e.dif[2]*lc[2]*(1.f/16384.f),0);
  lspe[count]=FVec4_New(e.spe[0]*lc[0]*(1.f/16384.f),e.spe[1]*lc[1]*(1.f/16384.f),e.spe[2]*lc[2]*(1.f/16384.f),0);
  ++count;
 }
 *base=FVec4_New(sum[0]*(1.f/32.f),sum[1]*(1.f/32.f),sum[2]*(1.f/32.f),0);
 for(unsigned i=1;i<4;++i)C3D_BoolUnifSet(GPU_VERTEX_SHADER,mesh_b_light[i],count>i);
 ++mesh_env_uploads;
}
static void mesh_buffers(){auto* b=C3D_GetBufInfo();BufInfo_Init(b);
 BufInfo_Add(b,mesh_positions,sizeof(native_mesh::Position),2,0x30);
 BufInfo_Add(b,mesh_dynamic,sizeof(native_mesh::Attribute),2,0x21);
}
static void mesh_bind_batch(unsigned i){
 const auto& run=mesh_frame?mesh_frame->topology->runs.at(i):mesh_plan.runs.at(i);const auto& b=mesh_batches.at(i);
 shader_bind(true);C3D_SetAttrInfo(&mesh_attributes);
 if(unified_shader_enabled()&&unified_draw_scope){if(unified_palette_run==i){++unified_palette_hits;return;}unified_palette_run=i;}
 for(unsigned slot=0;slot<run.matrix_count;++slot){const auto& matrix=mesh_frame?mesh_frame->matrices.at(run.matrices[slot]):mesh_source->matrices().at(run.matrices[slot]);
  for(unsigned j=0;j<4;++j)C3D_FVUnifSet(GPU_VERTEX_SHADER,mesh_u_objects+slot*4+j,
   matrix.fixed[j]/4096.f,matrix.fixed[4+j]/4096.f,matrix.fixed[8+j]/4096.f,matrix.fixed[12+j]/4096.f);
 }
 C3D_FVUnifSet(GPU_VERTEX_SHADER,mesh_u_texdims,b.tw?1.f/b.tw:0.f,b.th?1.f/b.th:0.f,0.f,0.f);
 mesh_stats.uniform_vectors+=run.matrix_count*4+1;
 if(mesh_frame && !mesh_frame->batch_env.empty()){
  const int env=mesh_frame->batch_env.at(i);
  if(env>=0 && env!=mesh_env_loaded){mesh_upload_env(mesh_frame->envs.at(size_t(env)));mesh_env_loaded=env;}
 }
}
static bool mesh_prepare(const native_mesh::Packet& packet){
 if(!packet.admitted()||!mesh_init()||g_direct_open)return false;
 const bool same=mesh_source==&packet&&mesh_generation==packet.generation()&&mesh_resources.external_unchanged();
 // Retained HELD geometry owns both linear bytes and texture snapshots.
 // No CPU write or replacement requires an acquire/drain on that branch.
 if(same){++mesh_stats.reuses;++mesh_stats.frames;return true;}
 const double drain=v3ds_time_ms();if(!C3D_FrameBegin(0))return false;
 end_gpu_frame();mesh_stats.drain_ms+=v3ds_time_ms()-drain;
 mesh_source=nullptr;mesh_generation=UINT64_MAX;mesh_frame.reset();
 g_direct_valid=false; // Any ordinary arena lease must be rebuilt after this path.
 const double prepare=v3ds_time_ms();
 if(!same){
  if(!mesh_plan.build(packet)||mesh_plan.positions.size()>32768||mesh_plan.indices.size()>32768)return false;
  mesh_batches.clear();for(const auto& r:mesh_plan.runs)mesh_batches.push_back(r.batch);
  if(!mesh_resources.freeze(mesh_batches.data(),mesh_batches.size()))return false;
  mesh_source=&packet;mesh_generation=packet.generation();++mesh_stats.builds;
  mesh_stats.vertices+=packet.vertices().size();mesh_stats.triangles+=packet.triangles().size();mesh_stats.matrices+=packet.matrices().size();mesh_stats.runs+=mesh_plan.runs.size();
 }else ++mesh_stats.reuses;
 mesh_stats.prepare_ms+=v3ds_time_ms()-prepare;
 const double upload=v3ds_time_ms();
 if(!same){
  const bool identical=mesh_position_snapshot.size()==mesh_plan.positions.size()&&mesh_index_snapshot.size()==mesh_plan.indices.size()&&
   !std::memcmp(mesh_position_snapshot.data(),mesh_plan.positions.data(),mesh_plan.positions.size()*sizeof(native_mesh::Position))&&
   !std::memcmp(mesh_index_snapshot.data(),mesh_plan.indices.data(),mesh_plan.indices.size()*2);
  if(!identical){
   mesh_position_snapshot=mesh_plan.positions;mesh_index_snapshot=mesh_plan.indices;
   const auto pbytes=mesh_plan.positions.size()*sizeof(native_mesh::Position),ibytes=mesh_plan.indices.size()*2;
   std::memcpy(mesh_positions,mesh_plan.positions.data(),pbytes);std::memcpy(mesh_indices,mesh_plan.indices.data(),ibytes);
   GSPGPU_FlushDataCache(mesh_positions,pbytes);GSPGPU_FlushDataCache(mesh_indices,ibytes);
   mesh_stats.position_bytes+=pbytes;mesh_stats.index_bytes+=ibytes;++mesh_stats.static_misses;
  }else ++mesh_stats.static_hits;
  const auto dbytes=mesh_plan.attributes.size()*sizeof(native_mesh::Attribute);
  std::memcpy(mesh_dynamic,mesh_plan.attributes.data(),dbytes);GSPGPU_FlushDataCache(mesh_dynamic,dbytes);mesh_stats.dynamic_bytes+=dbytes;
 }
 mesh_stats.upload_ms+=v3ds_time_ms()-upload;++mesh_stats.frames;return true;
}
static bool mesh_prepare(std::shared_ptr<const prepared_mesh::Frame> frame) try {
 frame_profile::Scope prepare_scope(frame_profile::WorkerMeshPrepare);
 if(!frame||!frame->topology||!mesh_init()||g_direct_open)return false;
 // Only Composer-published immutable Frames may enter this API.
 const auto& topology=*frame->topology;
 if(topology.positions.size()>32768||topology.indices.size()>32768||
    frame->attributes.size()!=topology.positions.size()||frame->batches.size()!=topology.runs.size())return false;
 // Packed DS normals need the unified shader's lighting (gpu-geometry-ingame).
 if(!frame->envs.empty() && (!unified_shader_enabled()||mesh_u_lbase<0||mesh_u_ldir<0||mesh_u_ldif<0||mesh_u_lspe<0||
    frame->batch_env.size()!=frame->batches.size()))return false;
 if(mesh_frame==frame&&mesh_resources.external_unchanged()){++mesh_stats.reuses;++mesh_stats.frames;return true;}
 const double drain=v3ds_time_ms();if(!C3D_FrameBegin(0))return false;
 end_gpu_frame();mesh_stats.drain_ms+=v3ds_time_ms()-drain;
 g_direct_valid=false;
 const bool same_topology=mesh_frame&&mesh_frame->topology==frame->topology;
 // A failed prepare must not admit HELD reuse of overwritten resources.
 mesh_source=nullptr;mesh_generation=UINT64_MAX;mesh_frame.reset();
 const double prepare=v3ds_time_ms();mesh_batches=frame->batches;
 if(!mesh_resources.freeze(mesh_batches.data(),mesh_batches.size()))return false;
 mesh_stats.prepare_ms+=v3ds_time_ms()-prepare;
 const double upload=v3ds_time_ms();
 if(!same_topology){
  // Keep snapshots coherent for the original Packet path when paths alternate.
  mesh_position_snapshot=topology.positions;mesh_index_snapshot=topology.indices;
  const auto pb=topology.positions.size()*sizeof(native_mesh::Position),ib=topology.indices.size()*2;
  if(pb){std::memcpy(mesh_positions,topology.positions.data(),pb);GSPGPU_FlushDataCache(mesh_positions,pb);}
  if(ib){std::memcpy(mesh_indices,topology.indices.data(),ib);GSPGPU_FlushDataCache(mesh_indices,ib);}
  mesh_stats.position_bytes+=pb;mesh_stats.index_bytes+=ib;++mesh_stats.static_misses;
 }else ++mesh_stats.static_hits;
 const auto db=frame->attributes.size()*sizeof(native_mesh::Attribute);
 if(db){std::memcpy(mesh_dynamic,frame->attributes.data(),db);GSPGPU_FlushDataCache(mesh_dynamic,db);}
 mesh_stats.dynamic_bytes+=db;mesh_stats.upload_ms+=v3ds_time_ms()-upload;
 mesh_stats.vertices+=topology.positions.size();mesh_stats.triangles+=topology.indices.size()/3;
 mesh_stats.matrices+=frame->matrices.size();mesh_stats.runs+=topology.runs.size();
 mesh_frame=std::move(frame);++mesh_stats.builds;++mesh_stats.frames;return true;
}catch(const std::bad_alloc&){
 // Allocating operations above occur only after a successful GPU drain.
 // A failed snapshot allocation may have changed only one CPU stream, so
 // neither that pair nor a prior Frame is a valid cache key for a later draw.
 mesh_frame.reset();mesh_source=nullptr;mesh_generation=UINT64_MAX;
 mesh_position_snapshot.clear();mesh_index_snapshot.clear();
 return false;
}
static bool mesh_destroy(){
 if(!mesh_dvlb)return true;
 if(g_direct_open)end_gpu_frame();if(!C3D_FrameBegin(0))return false;
 shader_bind(false);end_gpu_frame();mesh_active=false;
 mesh_frame.reset();mesh_source=nullptr;mesh_generation=UINT64_MAX;
 shaderProgramFree(&mesh_program);DVLB_Free(mesh_dvlb);mesh_dvlb=nullptr;
 linearFree(mesh_positions);linearFree(mesh_dynamic);linearFree(mesh_indices);
 mesh_positions=nullptr;mesh_dynamic=nullptr;mesh_indices=nullptr;return true;
}

// Mixed stream. Both strong frames and linear CPU bytes stay live
// through drain, centre/eyes and diagnostic captures. Any Composer-published occurrence count.
static native_mesh::FrameAdapter mixed_adapter;
static mixed_clip::Builder mixed_builder;
static std::shared_ptr<const mixed_clip::Frame> mixed_frame;
static V3dsVert* mixed_cpu=nullptr;
static uint64_t mixed_builds=0,mixed_held=0,mixed_native_draws=0,mixed_cpu_draws=0,mixed_upload_bytes=0,mixed_inside=0,mixed_crossing=0,mixed_rejected=0,mixed_nonpositive=0;
static double mixed_classifier_ms=0;
static uint64_t mixed_bulk_runs=0,mixed_bulk_triangles=0;
static bool mixed_prepare(std::shared_ptr<const prepared_mesh::Frame> frame){
 if(!frame)return false;
 if(mixed_frame&&mixed_frame->gpu==frame&&mesh_frame==frame){
  if(!mesh_prepare(frame))return false;++mixed_held;return true;
 }
 const double started=v3ds_time_ms();auto next=[&]{frame_profile::Scope s(frame_profile::WorkerClassify);return mixed_builder.build(frame);}();mixed_classifier_ms+=v3ds_time_ms()-started;
 if(!next||next->cpu.size()>32768){std::printf("Mixed build refused site=%d cpu=%u\n",mixed_clip::mixed_clip_failure,next?unsigned(next->cpu.size()):0u);return false;}
 if(!mixed_cpu){mixed_cpu=static_cast<V3dsVert*>(linearAlloc(32768*sizeof(V3dsVert)));if(!mixed_cpu)return false;}
 // mesh_prepare drains BEFORE retiring owners or writing any GPU storage.
 if(!mesh_prepare(frame)){std::puts("Mixed mesh_prepare refused");return false;}
 mixed_frame.reset();
 const auto bytes=next->cpu.size()*sizeof(V3dsVert);
 if(bytes){std::memcpy(mixed_cpu,next->cpu.data(),bytes);GSPGPU_FlushDataCache(mixed_cpu,bytes);}
 mixed_upload_bytes+=bytes;++mixed_builds;
 mixed_inside+=next->counts.inside_triangles;mixed_crossing+=next->counts.crossing_triangles;mixed_rejected+=next->counts.rejected_triangles;mixed_nonpositive+=next->counts.nonpositive_w_cpu_triangles;
 mixed_bulk_runs+=next->counts.bulk_inside_runs;mixed_bulk_triangles+=next->counts.bulk_inside_triangles;
 mixed_frame=std::move(next);return true;
}
static bool mixed_prepare(const native_mesh::Packet& packet){
 auto publication=mixed_adapter.capture(packet,0);return publication&&mixed_prepare(std::move(publication.frame));
}
