#include "SystemManager.h"

namespace Quasar {
SystemManager* SystemManager::instance = nullptr;

b8 SystemManager::init() {
    assert(!instance);
    instance = new SystemManager();

    for (int i=0; i<SYSTEM_MAX; i++) {
        instance->registered_systems[i] = nullptr;
    }
    return true;
}

void SystemManager::shutdown() {
    delete instance;
}

b8 SystemManager::Register(qs_system s_id, System* s, void* config) {
    if (s == nullptr) {
        LOG_ERROR("ptr to System [%d] can not be null", s_id)
        return false;
    }
    if (registered_systems[s_id] != nullptr) {
        LOG_ERROR("System [%d] already exists, can not override existing system", s_id)
        return false;
    }
    registered_systems[s_id] = s;
    return registered_systems[s_id]->init(config);
}

void SystemManager::Unregister(qs_system s_id) {
    registered_systems[s_id]->shutdown();
    QSMEM.free(registered_systems[s_id]);
    registered_systems[s_id] = nullptr;
}

}