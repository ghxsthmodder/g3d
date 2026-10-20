#include "g3d/model.hpp"
#include <cassert>
#include <string>
int main() {
 const std::string good=R"(<g3d version="1.0"><mesh><geometry id="g"/><primitive id="p" geometry="g" material="m"/></mesh><materials><material id="m"/></materials><scene><node id="root"><mesh ref="p"/></node></scene></g3d>)";
 auto d=g3d::parse(good); assert(d.valid()); assert(d.version=="1.0");
 assert(!g3d::parse(R"(<g3d version="2.0"><scene><node id="root"/></scene></g3d>)").valid());
 assert(!g3d::parse(R"(<g3d><scene><node id="root"/></scene></g3d>)").valid());
 assert(!g3d::parse(R"(<g3d version="1.0"><metadata/></g3d>)").valid());
 assert(!g3d::parse(R"(<g3d version="1.0"><scene></g3d>)").valid());
 assert(!g3d::parse(R"(<g3d version="1.0"><mesh><primitive id="p" geometry="missing" material="m"/></mesh><materials><material id="m"/></materials><scene><node id="root"><mesh ref="p"/></node></scene></g3d>)").valid());
 assert(!g3d::parse(R"(<g3d version="1.0"><scene><node id="root"/></scene><scene><node id="other"/></scene></g3d>)").valid());
 assert(!g3d::parse(R"(<g3d version="1.0"><materials/><mesh/><scene><node id="root"/></scene></g3d>)").valid());
 const std::string dup=R"(<g3d version="1.0"><assets><asset id="x" type="texture" src="a.png"/><asset id="x" type="texture" src="b.png"/></assets><scene><node id="root"/></scene></g3d>)";
 assert(!g3d::parse(dup).valid());
 return 0;
}
