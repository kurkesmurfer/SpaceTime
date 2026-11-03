// PanelGenerator import profile for StageTime. This file is intentionally not
// part of the Rack build; it exposes the composite controls and editable panel
// art in the direct createParam/createLight form PanelGenerator understands.
#include <rack.hpp>

struct StageTimePanelGeneratorProfile : rack::ModuleWidget {
    StageTimePanelGeneratorProfile(rack::Module* module) {
        setPanel(asset::plugin(pluginInstance,
            "../design/StageTime/StageTime-10HP-PanelGenerator.svg"));

        // Each DualSpringButton is one of the two touch zones under the shared
        // three-position spring-switch graphic in the SVG.
        addParam(createParamCentered<spacetime::DualSpringButton>(
            mm2px(Vec(26.8f, 22.1f)), module, StageTime::SYNC_PARAM));
        addParam(createParamCentered<spacetime::DualSpringButton>(
            mm2px(Vec(26.8f, 28.1f)), module, StageTime::FREE_PARAM));
        addParam(createParamCentered<spacetime::DualSpringButton>(
            mm2px(Vec(18.2f, 111.8f)), module, StageTime::INSTRUMENT_UP_PARAM));
        addParam(createParamCentered<spacetime::DualSpringButton>(
            mm2px(Vec(18.2f, 117.8f)), module, StageTime::INSTRUMENT_DOWN_PARAM));

        addChild(createLightCentered<SmallLight<RedLight>>(
            mm2px(Vec(34.3f, 22.1f)), module, StageTime::FREE_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(
            mm2px(Vec(34.3f, 27.8f)), module, StageTime::SYNC_LIGHT));
    }
};

// PanelGenerator reads this slug to name the imported module.
Model* modelStageTimePanelGeneratorProfile =
    createModel<StageTimePanelGeneratorProfile, StageTimePanelGeneratorProfile>("StageTime");
