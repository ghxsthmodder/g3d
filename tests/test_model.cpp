#include "g3d/model.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
namespace {
int failures = 0;
// assert() vanishes under NDEBUG, which Release builds set; the suite must fail
// loudly there too.
void require(bool condition, const std::string& what) {
    if (!condition) { std::printf("FAILED: %s\n", what.c_str()); ++failures; }
}
bool accepts(const std::string& xml) { return g3d::parse(xml).valid(); }
bool rejects(const std::string& xml) { return !g3d::parse(xml).valid(); }
} // namespace

int main() {
 const std::string good=R"(<g3d version="1.0"><mesh><geometry id="g"/><primitive id="p" geometry="g" material="m"/></mesh><materials><material id="m"/></materials><scene><node id="root"><mesh ref="p"/></node></scene></g3d>)";
 require(accepts(good), "minimal document");
 require(g3d::parse(good).version=="1.0", "version captured");
 require(rejects(R"(<g3d version="2.0"><scene><node id="root"/></scene></g3d>)"), "unsupported version");
 require(rejects(R"(<g3d><scene><node id="root"/></scene></g3d>)"), "missing version");
 require(rejects(R"(<g3d version="1.0"><metadata/></g3d>)"), "missing scene");
 require(rejects(R"(<g3d version="1.0"><scene></g3d>)"), "unclosed scene");
 require(rejects(R"(<g3d version="1.0"><mesh><primitive id="p" geometry="missing" material="m"/></mesh><materials><material id="m"/></materials><scene><node id="root"><mesh ref="p"/></node></scene></g3d>)"), "unresolved geometry");
 require(rejects(R"(<g3d version="1.0"><scene><node id="root"/></scene><scene><node id="other"/></scene></g3d>)"), "duplicate scene section");
 require(rejects(R"(<g3d version="1.0"><materials/><mesh/><scene><node id="root"/></scene></g3d>)"), "sections out of order");
 const std::string dup=R"(<g3d version="1.0"><assets><asset id="x" type="texture" src="a.png"/><asset id="x" type="texture" src="b.png"/></assets><scene><node id="root"/></scene></g3d>)";
 require(rejects(dup), "duplicate asset id");

 const std::string wide=R"(<g3d version="1.0"><assets><textureSet id="s"><image assetId="t"/></textureSet><asset id="t" type="texture" src="a.ktx2"/></assets><skeletons><skin id="k"/></skeletons><mesh><geometry id="g"/><primitive id="p" geometry="g" material="m"/></mesh><materials><material id="m"/></materials><cameras><camera id="c" type="perspective"/></cameras><lights><light id="l" type="point"/></lights><animations><animation id="a"><channel target="p" property="morphWeights"/></animation></animations><collisions/><scene><node id="root"><mesh ref="p"/><camera ref="c"/><light ref="l"/></node></scene></g3d>)";
 require(accepts(wide), "v1.0 section set with skeletons, cameras and lights");
 require(rejects(R"(<g3d version="1.0"><mesh/><skeletons/><scene><node id="root"/></scene></g3d>)"), "mesh before skeletons");
 require(rejects(R"(<g3d version="1.0"><lights/><cameras/><scene><node id="root"/></scene></g3d>)"), "lights before cameras");
 require(rejects(R"(<g3d version="1.0"><assets><textureSet id="s"><image assetId="missing"/></textureSet></assets><scene><node id="root"/></scene></g3d>)"), "image referencing unknown asset");
 require(rejects(R"(<g3d version="1.0"><assets><textureSet id="s"/><asset id="s" type="texture" src="a.ktx2"/></assets><scene><node id="root"/></scene></g3d>)"), "textureSet shares the texture id category");
 require(rejects(R"(<g3d version="1.0"><animations><animation id="a"><channel target="ghost" property="rotation"/></animation></animations><scene><node id="root"/></scene></g3d>)"), "channel targeting unknown id");

#ifdef G3D_SAMPLES_DIR
 const std::filesystem::path samples = G3D_SAMPLES_DIR;
 for (const char* name : {"v1/character.g3d", "primitives/triangle.g3d", "primitives/square.g3d",
                          "primitives/sphere.g3d", "primitives/cylinder.g3d", "primitives/torus.g3d",
                          "primitives/flat.g3d"}) {
  const auto path = samples / name;
  auto doc = g3d::load(path);
  require(doc.valid(), std::string("repository sample parses: ") + name);
  if (!doc.valid())
   for (const auto& d : doc.diagnostics) std::printf("  %s:%zu: %s\n", name, d.line, d.message.c_str());
 }
#endif

 if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
 std::printf("all checks passed\n");
 return 0;
}
