#include "CoreModules/CoreProcessor.hh"
#include "CoreModules/elements/element_counter.hh"
#include "CoreModules/elements/elements.hh"
#include "CoreModules/register_module.hh"
#include "MetaModuleBusProbe.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

enum ParamIds { BusIdParam, NumParams };
enum InputIds { SendInput, NumInputs };
enum OutputIds { SeenOutput, StatusOutput, NumOutputs };
enum LightIds { StatusDisplay, NumLights };

enum class ProbeStatus { Waiting, Linked, Duplicate };

spacetime::MetaModuleBusProbeRegistry probeRegistry;

uint32_t currentProcessorCore() {
	uint32_t affinity = 0;
	asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(affinity));
	return affinity & 0xff;
}

class ProbeModule : public CoreProcessor {
public:
	ProbeModule(bool core)
		: core_(core)
		, token_(probeRegistry.makeToken()) {
		registerEndpoint();
	}

	~ProbeModule() override {
		unregisterEndpoint();
	}

	void set_samplerate(float sampleRate) override {
		staleLimit_ = std::max(1u, (unsigned)(sampleRate * 0.25f));
	}

	void set_param(int paramId, float value) override {
		if (paramId != BusIdParam)
			return;
		unsigned next = std::min(3u, (unsigned)(value * 4.f));
		if (next == busId_)
			return;
		unregisterEndpoint();
		busId_ = next;
		lastPeerHeartbeat_ = 0;
		staleSamples_ = staleLimit_;
		registerEndpoint();
	}

	float get_param(int paramId) const override {
		return paramId == BusIdParam ? (float)busId_ / 3.f : 0.f;
	}

	void set_input(int inputId, float value) override {
		if (inputId == SendInput)
			input_ = std::max(-10.f, std::min(10.f, value));
	}

	float get_output(int outputId) const override {
		if (outputId == SeenOutput)
			return seen_;
		if (outputId == StatusOutput)
			return status_ == ProbeStatus::Linked ? 5.f :
				status_ == ProbeStatus::Duplicate ? -5.f : 0.f;
		return 0.f;
	}

	size_t get_display_text(int displayId, std::span<char> text) override {
		if (displayId != StatusDisplay || text.empty())
			return 0;
		const char* state = status_ == ProbeStatus::Linked ? "LINK" :
			status_ == ProbeStatus::Duplicate ? "DUP" : "WAIT";
		char label[8] = {};
		size_t stateLength = std::strlen(state);
		std::memcpy(label, state, stateLength);
		label[stateLength] = ' ';
		label[stateLength + 1] = 'C';
		uint32_t processorCore = processingCore_.load(std::memory_order_acquire);
		label[stateLength + 2] = processorCore == 0 ? '?' : (char)('0' + processorCore);
		size_t length = std::min(text.size(), stateLength + 3);
		std::memcpy(text.data(), label, length);
		return length;
	}

	void update() override {
		processingCore_.store(currentProcessorCore() + 1, std::memory_order_release);
		spacetime::MetaModuleProbeBus& bus = probeRegistry.bus(busId_);
		tryClaim();

		uint32_t selfCount = core_ ? bus.coreCount.load(std::memory_order_acquire) :
			bus.remoteCount.load(std::memory_order_acquire);
		uint32_t peerCount = core_ ? bus.remoteCount.load(std::memory_order_acquire) :
			bus.coreCount.load(std::memory_order_acquire);
		if (!ownsEndpoint_ || selfCount != 1 || peerCount > 1) {
			status_ = ProbeStatus::Duplicate;
			return;
		}

		std::atomic<uint32_t>& heartbeat = core_ ? bus.coreHeartbeat : bus.remoteHeartbeat;
		std::atomic<uint32_t>& peerHeartbeat = core_ ? bus.remoteHeartbeat : bus.coreHeartbeat;
		heartbeat.fetch_add(1, std::memory_order_release);

		spacetime::ProbeMailbox& outbound = core_ ? bus.coreToRemote : bus.remoteToCore;
		spacetime::ProbeMailbox& inbound = core_ ? bus.remoteToCore : bus.coreToRemote;
		outbound.publish(input_);
		inbound.read(lastInboundSequence_, seen_);

		uint32_t peerBeat = peerHeartbeat.load(std::memory_order_acquire);
		if (peerCount == 0 || peerBeat == lastPeerHeartbeat_) {
			if (staleSamples_ < staleLimit_)
				staleSamples_++;
		} else {
			lastPeerHeartbeat_ = peerBeat;
			staleSamples_ = 0;
		}
		status_ = peerCount == 1 && staleSamples_ < staleLimit_ ?
			ProbeStatus::Linked : ProbeStatus::Waiting;
	}

private:
	void registerEndpoint() {
		ownsEndpoint_ = core_ ? probeRegistry.registerCore(busId_, token_) :
			probeRegistry.registerRemote(busId_, token_);
		registered_ = true;
	}

	void unregisterEndpoint() {
		if (!registered_)
			return;
		if (core_)
			probeRegistry.unregisterCore(busId_, token_);
		else
			probeRegistry.unregisterRemote(busId_, token_);
		registered_ = false;
		ownsEndpoint_ = false;
	}

	void tryClaim() {
		if (ownsEndpoint_)
			return;
		ownsEndpoint_ = core_ ? probeRegistry.tryClaimCore(busId_, token_) :
			probeRegistry.tryClaimRemote(busId_, token_);
	}

	bool core_ = false;
	bool registered_ = false;
	bool ownsEndpoint_ = false;
	uint32_t token_ = 0;
	unsigned busId_ = 0;
	unsigned staleLimit_ = 12000;
	unsigned staleSamples_ = 12000;
	uint32_t lastPeerHeartbeat_ = 0;
	uint32_t lastInboundSequence_ = 0;
	float input_ = 0.f;
	float seen_ = 0.f;
	std::atomic<uint32_t> processingCore_{0};
	ProbeStatus status_ = ProbeStatus::Waiting;
};

class ProbeCore : public ProbeModule {
public:
	ProbeCore()
		: ProbeModule(true) {
	}
};

class ProbeRemote : public ProbeModule {
public:
	ProbeRemote()
		: ProbeModule(false) {
	}
};

template<typename Module>
void registerProbe(const char* slug, const char* description, const char* panel) {
	using namespace MetaModule;
	static std::array<Element, 5> elements;
	static std::array<ElementCount::Indices, 5> indices;

	AltParamChoiceLabeled busId;
	busId.short_name = "Instrument ID";
	busId.num_pos = 4;
	busId.default_value = 0;
	busId.pos_names = {"A", "B", "C", "D"};
	elements[0] = busId;
	indices[0] = {.param_idx = BusIdParam};

	DynamicTextDisplay display;
	display.x_mm = 4.f;
	display.y_mm = 16.f;
	display.coords = Coords::TopLeft;
	display.width_mm = 32.64f;
	display.height_mm = 10.f;
	display.short_name = "Link status";
	display.font = "Default_12";
	display.color = Colors565::White;
	elements[1] = display;
	indices[1] = {.light_idx = StatusDisplay};

	JackInput input;
	input.x_mm = 20.32f;
	input.y_mm = 54.f;
	input.image = "SpaceTimeProbe/components/jack.png";
	input.short_name = "Send";
	elements[2] = input;
	indices[2] = {.input_idx = SendInput};

	JackOutput seen;
	seen.x_mm = 20.32f;
	seen.y_mm = 82.f;
	seen.image = "SpaceTimeProbe/components/jack.png";
	seen.short_name = "Seen";
	elements[3] = seen;
	indices[3] = {.output_idx = SeenOutput};

	JackOutput status;
	status.x_mm = 20.32f;
	status.y_mm = 110.f;
	status.image = "SpaceTimeProbe/components/jack.png";
	status.short_name = "Status";
	elements[4] = status;
	indices[4] = {.output_idx = StatusOutput};

	ModuleInfoView info{
		.description = description,
		.width_hp = 8,
		.elements = elements,
		.indices = indices,
	};
	register_module<Module>("Kurkesmurfer", slug, info, panel);
}

} // namespace

void initProbeCore() {
	registerProbe<ProbeCore>("BusProbeCore", "SpaceTime shared-bus Core probe", "SpaceTimeProbe/ProbeCore.png");
}

void initProbeRemote() {
	registerProbe<ProbeRemote>("BusProbeRemote", "SpaceTime shared-bus Remote probe", "SpaceTimeProbe/ProbeRemote.png");
}
