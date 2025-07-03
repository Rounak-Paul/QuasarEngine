#pragma once 

extern Quasar::Engine* Quasar::CreateEngine();

int main(int argc, char** argv) {
	Quasar::Engine* app = Quasar::CreateEngine(); ;
	app->init();
	app->run();
	app->shutdown();
	delete app;

    return 0;
}