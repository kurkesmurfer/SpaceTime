#include "plugin.hpp"
#include "paneltheme.hpp"
#include "ChainAdapter.hpp"
#include "GlueBus.hpp"

#include <atomic>

namespace {

struct GlueRegistry {
	spacetime::GlueBus buses[spacetime::kGlueLinks];
	std::atomic<engine::Module*> leftEndpoints[spacetime::kGlueLinks];
	std::atomic<engine::Module*> rightEndpoints[spacetime::kGlueLinks];

	GlueRegistry() {
		for (int i = 0; i < spacetime::kGlueLinks; i++) {
			leftEndpoints[i].store(NULL);
			rightEndpoints[i].store(NULL);
		}
	}
};

GlueRegistry registry;

struct GlueEndpoint : Module {
	enum ParamId { PARAMS_LEN };
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LINK_LIGHT_GREEN, LINK_LIGHT_RED, LIGHTS_LEN };

	std::atomic<int> requestedLink;
	int claimedLink = -1;
	bool ownsLink = false;
	spacetime::GlueMode mode = spacetime::GLUE_MODE_NONE;
	spacetime::GlueMode attachedMode = spacetime::GLUE_MODE_NONE;
	bool leftFragmentEndpoint;
	uint32_t queueDrops = 0;
	dsp::ClockDivider divider;

	explicit GlueEndpoint(bool leftFragment)
		: requestedLink(0), leftFragmentEndpoint(leftFragment) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configLight(LINK_LIGHT_GREEN, "Glue link status");
		divider.setDivision(16);
	}

	~GlueEndpoint() override { releaseClaim(); }

	std::atomic<int64_t>& owner(spacetime::GlueBus& bus) {
		return leftFragmentEndpoint ? bus.leftOwner : bus.rightOwner;
	}

	std::atomic<uint8_t>& busMode(spacetime::GlueBus& bus) {
		return leftFragmentEndpoint ? bus.leftMode : bus.rightMode;
	}

	std::atomic<engine::Module*>& endpointSlot(int link) {
		return leftFragmentEndpoint ? registry.leftEndpoints[link] : registry.rightEndpoints[link];
	}

	void releaseClaim() {
		if (claimedLink < 0 || !ownsLink)
			return;
		spacetime::GlueBus& bus = registry.buses[claimedLink];
		busMode(bus).store(spacetime::GLUE_MODE_NONE, std::memory_order_release);
		endpointSlot(claimedLink).store(NULL, std::memory_order_release);
		int64_t expected = id;
		owner(bus).compare_exchange_strong(expected, -1, std::memory_order_acq_rel);
		bus.epoch.fetch_add(1, std::memory_order_acq_rel);
		ownsLink = false;
		claimedLink = -1;
	}

	void syncClaim() {
		int desired = clamp(requestedLink.load(std::memory_order_relaxed), 0,
		                    spacetime::kGlueLinks - 1);
		if (claimedLink != desired)
			releaseClaim();
		if (ownsLink || id < 0)
			return;
		claimedLink = desired;
		spacetime::GlueBus& bus = registry.buses[claimedLink];
		int64_t expected = -1;
		ownsLink = owner(bus).compare_exchange_strong(
			expected, id, std::memory_order_acq_rel) || expected == id;
		if (ownsLink) {
			endpointSlot(claimedLink).store(this, std::memory_order_release);
			busMode(bus).store(mode, std::memory_order_release);
			bus.epoch.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	void publishMode(spacetime::GlueMode next) {
		if (next == mode)
			return;
		mode = next;
		if (ownsLink && claimedLink >= 0) {
			spacetime::GlueBus& bus = registry.buses[claimedLink];
			busMode(bus).store(mode, std::memory_order_release);
			bus.epoch.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	bool linked() const {
		return ownsLink && claimedLink >= 0 && registry.buses[claimedLink].linked();
	}

	const char* modeName() const {
		return mode == spacetime::GLUE_MODE_HEAD ? "HEAD" :
			(mode == spacetime::GLUE_MODE_STAGE ? "STAGE" : "NONE");
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "link", json_integer(requestedLink.load() + 1));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* value = json_object_get(root, "link");
		if (value)
			requestedLink.store(clamp((int)json_integer_value(value) - 1, 0,
			                          spacetime::kGlueLinks - 1));
	}

	virtual spacetime::GlueMode detectMode() const = 0;
	virtual void attachForMode(spacetime::GlueMode next) = 0;
	virtual void transport(spacetime::GlueBus& bus, uint32_t epoch, bool active) = 0;

	void process(const ProcessArgs& args) override {
		if (!divider.process())
			return;
		float dt = args.sampleTime * divider.getDivision();
		spacetime::GlueMode next = detectMode();
		if (next != attachedMode) {
			attachForMode(next);
			attachedMode = next;
		}
		publishMode(next);
		syncClaim();

		bool active = linked();
		if (ownsLink && claimedLink >= 0) {
			spacetime::GlueBus& bus = registry.buses[claimedLink];
			uint32_t epoch = bus.epoch.load(std::memory_order_acquire);
			transport(bus, epoch, active);
			queueDrops = bus.headsRight.drops() + bus.blocksRight.drops() +
				bus.headsLeft.drops() + bus.blocksLeft.drops();
		}

		lights[LINK_LIGHT_GREEN].setBrightnessSmooth(active ? 1.f : 0.f, dt);
		lights[LINK_LIGHT_RED].setBrightnessSmooth(
			(mode != spacetime::GLUE_MODE_NONE && !active) ? 1.f : 0.f, dt);
	}
};

struct GlueRight : GlueEndpoint {
	spacetime::MessagePort<spacetime::HeadsToAnchorMsg> headsIn;
	spacetime::MessagePort<spacetime::AnchorToBlocksMsg> blocksIn;

	GlueRight() : GlueEndpoint(true) {}

	spacetime::GlueMode detectMode() const override {
		engine::Module* neighbor = leftExpander.module;
		if (spacetime::modelIs(neighbor, modelHead, modelMidi, modelHeadAll))
			return spacetime::GLUE_MODE_HEAD;
		if (spacetime::modelIs(neighbor, modelProgram, modelStage4))
			return spacetime::GLUE_MODE_STAGE;
		return spacetime::GLUE_MODE_NONE;
	}

	void attachForMode(spacetime::GlueMode next) override {
		leftExpander.producerMessage = NULL;
		leftExpander.consumerMessage = NULL;
		if (next == spacetime::GLUE_MODE_HEAD)
			headsIn.attach(leftExpander);
		else if (next == spacetime::GLUE_MODE_STAGE)
			blocksIn.attach(leftExpander);
	}

	template <typename T>
	void sendLeft(const T& value) {
		engine::Module* neighbor = leftExpander.module;
		if (!neighbor || !neighbor->rightExpander.producerMessage)
			return;
		*(T*)neighbor->rightExpander.producerMessage = value;
		neighbor->rightExpander.requestMessageFlip();
	}

	void transport(spacetime::GlueBus& bus, uint32_t epoch, bool active) override {
		if (mode == spacetime::GLUE_MODE_HEAD) {
			if (active)
				bus.headsRight.push(epoch, *headsIn.consume(leftExpander));
			spacetime::AnchorToHeadsMsg out;
			if (active && bus.headsLeft.pop(epoch, out))
				sendLeft(out);
			else if (!active)
				sendLeft(out);
		}
		else if (mode == spacetime::GLUE_MODE_STAGE) {
			if (active)
				bus.blocksRight.push(epoch, *blocksIn.consume(leftExpander));
			spacetime::BlockToAnchorMsg out;
			if (active && bus.blocksLeft.pop(epoch, out))
				sendLeft(out);
			else if (!active)
				sendLeft(out);
		}
	}
};

struct GlueLeft : GlueEndpoint {
	spacetime::MessagePort<spacetime::AnchorToHeadsMsg> headsIn;
	spacetime::MessagePort<spacetime::BlockToAnchorMsg> blocksIn;

	GlueLeft() : GlueEndpoint(false) {}

	spacetime::GlueMode detectMode() const override {
		engine::Module* neighbor = rightExpander.module;
		if (spacetime::modelIs(neighbor, modelHead, modelMidi, modelProgram))
			return spacetime::GLUE_MODE_HEAD;
		if (spacetime::modelIs(neighbor, modelStage4))
			return spacetime::GLUE_MODE_STAGE;
		return spacetime::GLUE_MODE_NONE;
	}

	void attachForMode(spacetime::GlueMode next) override {
		rightExpander.producerMessage = NULL;
		rightExpander.consumerMessage = NULL;
		if (next == spacetime::GLUE_MODE_HEAD)
			headsIn.attach(rightExpander);
		else if (next == spacetime::GLUE_MODE_STAGE)
			blocksIn.attach(rightExpander);
	}

	template <typename T>
	void sendRight(const T& value) {
		engine::Module* neighbor = rightExpander.module;
		if (!neighbor || !neighbor->leftExpander.producerMessage)
			return;
		*(T*)neighbor->leftExpander.producerMessage = value;
		neighbor->leftExpander.requestMessageFlip();
	}

	void transport(spacetime::GlueBus& bus, uint32_t epoch, bool active) override {
		if (mode == spacetime::GLUE_MODE_HEAD) {
			if (active)
				bus.headsLeft.push(epoch, *headsIn.consume(rightExpander));
			spacetime::HeadsToAnchorMsg out;
			if (active && bus.headsRight.pop(epoch, out))
				sendRight(out);
			else if (!active)
				sendRight(out);
		}
		else if (mode == spacetime::GLUE_MODE_STAGE) {
			if (active)
				bus.blocksLeft.push(epoch, *blocksIn.consume(rightExpander));
			spacetime::AnchorToBlocksMsg out;
			if (active && bus.blocksRight.pop(epoch, out))
				sendRight(out);
			else if (!active)
				sendRight(out);
		}
	}
};

struct GlueReadout : Widget {
	GlueEndpoint* module = NULL;

	void draw(const DrawArgs& args) override {
		auto font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		int link = module ? module->requestedLink.load() + 1 : 1;
		const char* mode = module && module->mode == spacetime::GLUE_MODE_HEAD ? "H" :
			(module && module->mode == spacetime::GLUE_MODE_STAGE ? "S" : "-");
		std::string value = string::f("%d%s", link, mode);
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 9.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xcf, 0xc2, 0xaf));
		nvgText(args.vg, 0, 0, value.c_str(), NULL);
	}
};

template <typename TModule>
struct GlueWidget : ModuleWidget {
	explicit GlueWidget(TModule* module, const char* panel, const char* direction) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, panel)));
#ifndef METAMODULE
		spacetime::addMicroLabel(this, 5.08f, 6.f, "GLUE");
		spacetime::addLabel(this, 5.08f, 12.f, direction, spacetime::fontTitle(),
		                    13.f, spacetime::colorTitle());
		spacetime::addMicroLabel(this, 5.08f, 18.f, "LINK");
		addChild(new spacetime::CornerMark(10.16f, 128.5f, 0.70f, 1.04f, 4.f));
#endif
		addChild(createLightCentered<MediumLight<GreenRedLight>>(
			mm2px(Vec(5.08f, 25.f)), module, GlueEndpoint::LINK_LIGHT_GREEN));
		GlueReadout* readout = new GlueReadout;
		readout->module = module;
		readout->box.pos = mm2px(Vec(5.08f, 36.f));
		addChild(readout);
	}

	void appendContextMenu(Menu* menu) override {
		TModule* module = getModule<TModule>();
		menu->addChild(new MenuSeparator);
		if (!module)
			return;
		std::vector<std::string> labels;
		for (int i = 1; i <= spacetime::kGlueLinks; i++)
			labels.push_back(string::f("Link %d", i));
		menu->addChild(createIndexSubmenuItem("Glue link", labels,
			[=]() { return module->requestedLink.load(); },
			[=](int value) { module->requestedLink.store(value); }));
		menu->addChild(createMenuLabel(string::f("Detected side: %s", module->modeName())));
		menu->addChild(createMenuLabel(module->linked() ? "Status: linked" :
			(module->ownsLink ? "Status: waiting or mismatched" : "Status: duplicate link")));
		menu->addChild(createMenuLabel(string::f("Queue drops: %u", module->queueDrops)));
	}
};

struct GlueLeftWidget : GlueWidget<GlueLeft> {
	explicit GlueLeftWidget(GlueLeft* module)
		: GlueWidget<GlueLeft>(module, "res/GlueLeft.svg", "<") {}
};

struct GlueRightWidget : GlueWidget<GlueRight> {
	explicit GlueRightWidget(GlueRight* module)
		: GlueWidget<GlueRight>(module, "res/GlueRight.svg", ">") {}
};

} // namespace

engine::Module* gluePartnerOutward(engine::Module* endpoint) {
	if (!endpoint)
		return NULL;
	GlueEndpoint* glue = NULL;
	if (endpoint->model == modelGlueRight)
		glue = static_cast<GlueRight*>(endpoint);
	else if (endpoint->model == modelGlueLeft)
		glue = static_cast<GlueLeft*>(endpoint);
	if (!glue || !glue->linked() || glue->claimedLink < 0)
		return NULL;

	engine::Module* partner = glue->leftFragmentEndpoint ?
		registry.rightEndpoints[glue->claimedLink].load(std::memory_order_acquire) :
		registry.leftEndpoints[glue->claimedLink].load(std::memory_order_acquire);
	if (!partner)
		return NULL;
	return glue->leftFragmentEndpoint ? partner->rightExpander.module :
		partner->leftExpander.module;
}

Model* modelGlueLeft = createModel<GlueLeft, GlueLeftWidget>("GlueLeft");
Model* modelGlueRight = createModel<GlueRight, GlueRightWidget>("GlueRight");
