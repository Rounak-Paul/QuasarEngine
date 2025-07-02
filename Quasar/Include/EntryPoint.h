#pragma once 

extern Quasar::Application* Quasar::CreateApplication();

int main(int argc, char** argv) {
	auto app = Quasar::CreateApplication();
	app->init();
	app->run();
	app->shutdown();
	delete app;

    return 0;
}