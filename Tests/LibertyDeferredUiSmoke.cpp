#include "LibertyDeferredUi.h"
#include <cassert>
namespace {
int created=0,destroyed=0;
struct MockUi {
    MockUi(){++created;}
    ~MockUi(){++destroyed;}
    int get() const {return 42;}
};
LibertyDeferredUi::Deferred<MockUi> normal;
LibertyDeferredUi::Deferred<MockUi> early{true};
}
int main(){
    assert(created==0 && destroyed==0);
    LibertyDeferredUi::startAll();
    assert(created==2 && normal->get()==42 && early->get()==42);
    LibertyDeferredUi::startAll();
    assert(created==2);
    LibertyDeferredUi::stopEarly();
    assert(destroyed==1 && normal->get()==42);
    LibertyDeferredUi::stopEarly();
    assert(destroyed==1);
    LibertyDeferredUi::stopAll();
    assert(destroyed==2);
    LibertyDeferredUi::stopAll();
    assert(destroyed==2);
}
