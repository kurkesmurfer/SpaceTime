#pragma once
// SpaceTime — thin Rack adapter for the expander chain protocol (WP4).
// All protocol logic lives in dsp/Chain.hpp (pure, unit-tested host-side);
// this header only maps it onto Rack's leftExpander/rightExpander plumbing.
//
// Rack expander convention used here (as in Fundamental):
//   * the RECEIVER owns the double buffer for the side facing the sender,
//     allocated with the module (MessagePort member) and attached in the
//     module constructor — never in process();
//   * the SENDER writes into the receiver's producerMessage and calls
//     requestMessageFlip() on the receiver's Expander;
//   * the receiver reads its consumerMessage each tick.
//
// Wiring summary (done in WP7):
//   PROGRAM: rightExpander port <BlockToAnchorMsg>   (tables from blocks)
//            leftExpander  port <HeadsToAnchorMsg>   (statuses from heads)
//            writes AnchorToBlocksMsg into right neighbour (Stage4),
//            writes AnchorToHeadsMsg  into left  neighbour (Head).
//   STAGE4:  rightExpander port <BlockToAnchorMsg>   (table from next block)
//            leftExpander  port <AnchorToBlocksMsg>  (ops/status from anchor side)
//            writes BlockToAnchorMsg  into left  neighbour (Stage4 or Program),
//            writes AnchorToBlocksMsg into right neighbour (Stage4).
//   HEAD:    rightExpander port <AnchorToHeadsMsg>   (broadcast from anchor side)
//            leftExpander  port <HeadsToAnchorMsg>   (statuses from further heads)
//            writes HeadsToAnchorMsg  into right neighbour (Head or Program),
//            writes AnchorToHeadsMsg  into left  neighbour (Head).

#include <rack.hpp>
#include "plugin.hpp"
#include "Chain.hpp"

namespace spacetime {

// Double-buffered receive port; the owning module attaches it to the Expander
// facing the sender in its constructor.
template <typename T>
struct MessagePort {
	T messages[2];

	void attach(rack::engine::Module::Expander& exp) {
		exp.producerMessage = &messages[0];
		exp.consumerMessage = &messages[1];
	}

	// The receiver's view of the last flipped message.
	const T* consume(const rack::engine::Module::Expander& exp) const {
		return (const T*)exp.consumerMessage;
	}
};

// Does this module match one of the expected models?
inline bool modelIs(rack::engine::Module* m, rack::plugin::Model* m1,
                    rack::plugin::Model* m2 = NULL,
                    rack::plugin::Model* m3 = NULL) {
	return m && (m->model == m1 || (m2 && m->model == m2) ||
		(m3 && m->model == m3));
}

// Sender-side: producer buffer of the RIGHT neighbour (its leftExpander faces
// us). Returns NULL if the neighbour is absent or not an expected model.
template <typename T>
T* rightNeighborProducer(rack::engine::Module* self, rack::plugin::Model* m1,
                         rack::plugin::Model* m2 = NULL,
                         rack::plugin::Model* m3 = NULL) {
	rack::engine::Module* n = self->rightExpander.module;
	if (!modelIs(n, m1, m2, m3))
		return NULL;
	return (T*)n->leftExpander.producerMessage;
}

template <typename T>
T* leftNeighborProducer(rack::engine::Module* self, rack::plugin::Model* m1,
                        rack::plugin::Model* m2 = NULL,
                        rack::plugin::Model* m3 = NULL) {
	rack::engine::Module* n = self->leftExpander.module;
	if (!modelIs(n, m1, m2, m3))
		return NULL;
	return (T*)n->rightExpander.producerMessage;
}

inline void flipRightNeighbor(rack::engine::Module* self) {
	if (self->rightExpander.module)
		self->rightExpander.module->leftExpander.requestMessageFlip();
}

inline void flipLeftNeighbor(rack::engine::Module* self) {
	if (self->leftExpander.module)
		self->leftExpander.module->rightExpander.requestMessageFlip();
}

// ---- NeighborView over the live Rack module graph ---------------------------
// Used by the anchor for enumerateChain() (stage-count display, broken-chain
// warning). Classification by Model pointers, walking expander links.
struct RackNeighborView : NeighborView {
	rack::engine::Module* anchor;
	rack::plugin::Model* programModel;
	rack::plugin::Model* stage4Model;
	rack::plugin::Model* headModel;
	rack::plugin::Model* midiModel;

	RackNeighborView(rack::engine::Module* anchor, rack::plugin::Model* program,
	                 rack::plugin::Model* stage4, rack::plugin::Model* head,
	                 rack::plugin::Model* midi)
		: anchor(anchor), programModel(program), stage4Model(stage4),
		  headModel(head), midiModel(midi) {}

	ModuleType classify(rack::engine::Module* m) const {
		if (!m)
			return ModuleType::None;
		if (m->model == stage4Model)
			return ModuleType::Stage4;
		if (m->model == headModel)
			return ModuleType::Head;
		if (m->model == midiModel)
			return ModuleType::Midi;
		if (m->model == programModel)
			return ModuleType::Program;
		return ModuleType::Foreign;
	}

	rack::engine::Module* next(rack::engine::Module* m, bool right) const {
		if (!m)
			return NULL;
		rack::engine::Module* neighbor = right ? m->rightExpander.module :
			m->leftExpander.module;
		if ((right && neighbor && neighbor->model == modelGlueRight) ||
		    (!right && neighbor && neighbor->model == modelGlueLeft))
			return gluePartnerOutward(neighbor);
		return neighbor;
	}

	ModuleType rightAt(int n) const override {
		rack::engine::Module* m = anchor;
		for (int i = 0; i < n && m; i++)
			m = next(m, true);
		return classify(m);
	}

	ModuleType leftAt(int n) const override {
		rack::engine::Module* m = anchor;
		for (int i = 0; i < n && m; i++)
			m = next(m, false);
		return classify(m);
	}
};

} // namespace spacetime
