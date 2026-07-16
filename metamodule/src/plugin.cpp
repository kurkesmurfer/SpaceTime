void initProbeCore();
void initProbeRemote();

extern "C" void init() {
	initProbeCore();
	initProbeRemote();
}
