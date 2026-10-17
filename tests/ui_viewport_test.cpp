#include "UiViewport.h"

int main() {
    const UiViewport desktop = makeUiViewport(1920, 1080);
    if (desktop.width != 1280 || desktop.height != 720) return 1;

    const UiViewport landscapePhone = makeUiViewport(844, 390);
    if (landscapePhone.width != 1280 || landscapePhone.height != 591) return 2;

    const UiViewport ultrawide = makeUiViewport(2560, 1080);
    if (ultrawide.width != 1280 || ultrawide.height != 560) return 3;

    const UiViewport unavailable = makeUiViewport(0, 0);
    if (unavailable.width != 1280 || unavailable.height != 720) return 4;

    return 0;
}
