#pragma once
#include "gpu_composite.h"
#include <optional>

namespace ds_gpu {
// Candidate fusion of an ordinary OBJ GPU layer with prepared BG candidates.
// No geometry, windows, alpha OBJ or alpha effect is admitted by this slice.
class NativeObjMerge {
public:
    bool init();
    void destroy();
    static bool supported(ntr::PpuPreparedCandidates background);
    // Wait for the previous GPU queue before updating this immutable input.
    void update(ntr::PpuPreparedCandidates background);
    void compose(Composite& compositor,Surface& output,C3D_Tex& objects,
                 const ntr::PpuCompositionState& background);
    // Legacy fixtures borrow scene planes, without trusted cache generations.
    static bool supported(const ntr::PpuPreparedScene& scene){
        return supported({scene,scene.front});
    }
    void update(const ntr::PpuPreparedScene& scene){update({scene,scene.front});}
    void compose(Composite& compositor,Surface& output,C3D_Tex& objects,
                 const ntr::PpuPreparedScene& scene){
        compose(compositor,output,objects,ntr::PpuPreparedCandidates(scene,scene.front).state);
    }
private:
    C3D_Tex priority_{};
    uint8_t* snapshot_=nullptr;
    uint32_t* pixels_=nullptr;
    const ntr::PpuBackgroundPlanes* identity_=nullptr;
    uint64_t generation_=0;
    bool valid_=false;
    uint64_t bands_[24]{};bool bands_valid_=false;
    bool gpu_priority_=false; // castle-30fps: priority comes from Composite::affine_priority()
    // minigames-2d-gpu: priority comes from the compositor's encoded front
    // texture alpha (same 32*(p+2) values); pending_ keeps the view for the
    // CPU plane if that compositor did not encode this frame.
    bool front_priority_=false;
    std::optional<ntr::PpuPreparedCandidates> pending_;
    void update_cpu(ntr::PpuPreparedCandidates background);
};
}
