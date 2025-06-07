#pragma once 

extern Quasar::Engine* Quasar::CreateEngine();

int main(int argc, char** argv) {
	auto app = Quasar::CreateEngine();
	app->run();
	delete app;

    return 0;
}