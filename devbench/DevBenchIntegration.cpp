#include "DevBenchIntegration.h"

#include "DevBenchBridge.h"
#include "DevBenchSneakMatrix.h"

void DevBenchIntegration::Register() {
    DevBenchBridge::Register();
    DevBenchSneakMatrix::Register();
}
