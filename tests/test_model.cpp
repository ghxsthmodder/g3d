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

// A rejected case is expected to report, so it stays quiet. An accepted case that fails
// prints its diagnostics: that is what makes a new rule debuggable in one build.
bool accepts(const std::string& xml) {
    const auto parsed = g3d::parse(xml);
    if (parsed.valid()) return true;
    for (const auto& d : parsed.diagnostics) std::printf("    %s\n", d.message.c_str());
    return false;
}
bool rejects(const std::string& xml) { return !g3d::parse(xml).valid(); }
/// Conforming but noted: the severity split is itself a rule (BINARY.md §4).
bool warns(const std::string& xml) {
    const auto parsed = g3d::parse(xml);
    return parsed.valid() && !parsed.diagnostics.empty();
}

const char* triangle =
    R"(<geometry id="g"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry>)";

// Every case states only the rule it tests: `head` goes before <mesh> (metadata, assets,
// skeletons), `mesh_extra` beside the standard triangle, `sections` after materials.
// g/p/m and a one-node scene always exist and may be referenced.
std::string make_doc(const std::string& head = "", const std::string& mesh_extra = "",
                     const std::string& sections = "", const std::string& scene = R"(<node id="root"/>)") {
    return "<g3d version=\"1.0\">" + head + "<mesh>" + triangle + mesh_extra +
           R"(<primitive id="p" geometry="g" material="m"/>)" + "</mesh>" +
           R"(<materials><material id="m"/></materials>)" + sections +
           "<scene>" + scene + "</scene></g3d>";
}

std::string geometry_doc(const std::string& fragment, const std::string& head = "") {
    return make_doc(head, fragment);
}

std::string material_doc(const std::string& body, const std::string& attrs = "", const std::string& head = "") {
    return "<g3d version=\"1.0\">" + head + "<mesh>" + triangle +
           R"(<primitive id="p" geometry="g" material="m"/>)" +
           "</mesh><materials><material id=\"m\"" + attrs + ">" + body +
           "</material></materials><scene><node id=\"root\"><mesh ref=\"p\"/></node></scene></g3d>";
}

// A document whose streams live in the pool.
std::string pooled(const std::string& streams, const std::string& buffers =
                       R"(<buffer id="b" byteOffset="0" byteLength="65536" align="4"/>)",
                   const std::string& views =
                       R"(<bufferView id="v" buffer="b" byteOffset="0" byteLength="36"/>
                          <bufferView id="vi" buffer="b" byteOffset="64" byteLength="6" target="elementArrayBuffer"/>)") {
    return "<g3d version=\"1.0\"><mesh><pool src=\"m.bin\" byteLength=\"65536\">" + buffers + "</pool>" +
           views + "<geometry id=\"g\">" + streams + "</geometry>" +
           R"(<primitive id="p" geometry="g" material="m"/>)" +
           "</mesh><materials><material id=\"m\"/></materials>"
           "<scene><node id=\"root\"><mesh ref=\"p\"/></node></scene></g3d>";
}

const char* variant_set =
    R"(<assets><textureSet id="s"><image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*" default="true"/></textureSet>)"
    R"(<asset id="t" type="texture" src="a.ktx2"/></assets>)";

const char* array_set =
    R"(<assets><textureSet id="s"><image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*" default="true"/></textureSet>)"
    R"(<asset id="t" type="texture" src="a.ktx2" kind="2dArray"/></assets>)";

const char* rig =
    R"(<skeletons><skeleton id="rig" nodes="a b"><joint id="a" parent=""/><joint id="b" parent="a"/>)"
    R"(<inverseBindMatrices count="2">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</inverseBindMatrices>)"
    R"(</skeleton><skin id="sk" skeleton="rig" maxInfluences="4"><bind node="a" mode="linear"/></skin></skeletons>)";

const char* skin_streams =
    R"(<joints0 count="3">0 0 0 0 0 0 0 0 0 0 0 0</joints0><weights0 count="3">1 1 1 1 1 1 1 1 1 1 1 1</weights0>)";

const std::string good = make_doc();
} // namespace

int main() {
    // ---- structure, versions, ids, references (§1–§4) ---------------------------------
    require(accepts(good), "minimal conforming document");
    require(g3d::parse(good).version == "1.0", "version captured");
    require(rejects(R"(<g3d version="2.0"><scene><node id="root"/></scene></g3d>)"), "unsupported version");
    require(rejects(R"(<g3d><scene><node id="root"/></scene></g3d>)"), "missing version");
    require(rejects(R"(<g3d version="1.0"><metadata/></g3d>)"), "missing scene");
    require(rejects(R"(<g3d version="1.0"><scene></g3d>)"), "unclosed scene");
    require(rejects(R"(<g3d version="1.0"><scene/></g3d>)"), "scene with no node");
    require(rejects(make_doc("", "", "", R"(<node id="root"><mesh ref="ghost"/></node>)")), "unresolved primitive");
    require(rejects(R"(<g3d version="1.0"><mesh><geometry id="g"/></mesh><materials><material id="m"/></materials><scene><node id="root"/></scene></g3d>)"),
                "a primitive requires a material");
    require(rejects(R"(<g3d version="1.0"><scene><node id="root"/></scene><scene><node id="other"/></scene></g3d>)"), "duplicate scene section");
    require(rejects(R"(<g3d version="1.0"><materials/><mesh/><scene><node id="root"/></scene></g3d>)"), "sections out of order");
    require(rejects(R"(<g3d version="1.0"><mesh/><scene><node id="root"/></scene><bogus/>)"), "unknown top-level section");
    require(rejects(R"(<g3d version="1.0"><scene><node id="root"/><node id="other"/></scene></g3d>)"), "two root nodes");
    require(rejects(R"(<g3d version="1.0"><scene><node id="1bad"/></scene></g3d>)"), "an id may not start with a digit");
    require(rejects(R"(<g3d version="1.0"><assets><asset id="x" type="texture" src="a.png"/><asset id="x" type="texture" src="b.png"/></assets><scene><node id="root"/></scene></g3d>)"),
                "duplicate asset id");
    require(rejects(geometry_doc(R"(<geometry id="g2"/><geometry id="g2"/>)")), "duplicate geometry id");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2"/>)")), "a primitive requires a geometry");
    require(accepts(R"(<g3d version="1.0"><mesh><geometry id="body"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry><primitive id="body" geometry="body" material="body"/></mesh><materials><material id="body"/></materials><scene><node id="body"><mesh ref="body"/></node></scene></g3d>)"),
                "the same id in different categories resolves per category");

    // ---- §6 numbers -------------------------------------------------------------------
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="+3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "a '+' prefix is not a number");
    require(rejects(make_doc("", "", "", R"(<node id="root" translation="0 0"/>)")), "translation must have three components");
    require(rejects(make_doc("", "", "", R"(<node id="root" rotation="0 0 0 0"/>)")), "a rotation quaternion must be normalised");
    require(rejects(make_doc("", "", "", R"(<node id="root" scale="1 0 1"/>)")), "scale components must be non-zero");
    require(accepts(make_doc("", "", "", R"(<node id="root" rotation="0 0.70710678 0 0.70710678" scale="1 -1 1" translation="0 0 0"/>)")),
                "a mirrored scale and a unit quaternion are valid");

    // ---- §8 assets, variants and slots ------------------------------------------------
    require(accepts(make_doc(variant_set)), "one default variant per set");
    require(rejects(make_doc(R"(<assets><asset id="t" type="texture" src="a.ktx2" width="512"/></assets>)")),
                "the KTX2 header owns the dimensions");
    require(rejects(make_doc(R"(<assets><asset id="t" type="texture" src="a.ktx2" kind="3d"/></assets>)")),
                "asset kind must be 2d, cube or 2dArray");
    require(rejects(make_doc(R"(<assets><textureSet id="s"><image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                               <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_UNORM" when="*"/></textureSet>
                               <asset id="t" type="texture" src="a.ktx2"/></assets>)")),
                "a textureSet needs exactly one default");
    require(rejects(make_doc(R"(<assets><textureSet id="s"><image assetId="t" vkFormat="R8G8B8A8_SRGB" when="*" default="true"/></textureSet>
                               <asset id="t" type="texture" src="a.ktx2"/></assets>)")),
                "vkFormat must be a VK_FORMAT_ token");
    require(rejects(make_doc(R"(<assets><textureSet id="s"><image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="astc_ldc" default="true"/></textureSet>
                               <asset id="t" type="texture" src="a.ktx2"/></assets>)")),
                "when must be in the capability grammar");
    require(rejects(make_doc(R"(<assets><textureSet id="s"><image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="formatSupported(" default="true"/></textureSet>
                               <asset id="t" type="texture" src="a.ktx2"/></assets>)")),
                "an unclosed when call is not a condition");
    require(accepts(make_doc(R"(<assets><textureSet id="s">
                               <image assetId="t" vkFormat="VK_FORMAT_ASTC_4x4_SRGB_BLOCK" when="formatSupported(ASTC_4x4_SRGB) and apiLevel >= 26" prefer="30"/>
                               <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_UNORM" when='not hasExtension("VK_KHR_swapchain") or *' default="true"/>
                               </textureSet><asset id="t" type="texture" src="a.ktx2"/></assets>)")),
                "the when DSL parses its documented forms");
    require(rejects(material_doc(R"(<pbr><baseColor><texture assetId="s" texCoord="0" layer="1"/></baseColor></pbr>)", "", variant_set)),
                "layer requires a 2dArray image");
    require(accepts(material_doc(R"(<pbr><emissive r="0" g="0" b="0"><texture assetId="s" texCoord="0" layer="1"/></emissive></pbr>)", "", array_set)),
                "layer reads a slice of a 2dArray");
    require(rejects(material_doc(R"(<pbr><normal><texture assetId="s" colorspace="linear"/></normal></pbr>)", "", variant_set)),
                "a linear slot must not resolve to an SRGB format");
    require(rejects(material_doc(R"(<pbr><baseColor><texture assetId="s" colorspace="p3"/></baseColor></pbr>)", "", variant_set)),
                "colorspace must be srgb or linear");
    require(rejects(material_doc(R"(<pbr><baseColor><texture assetId="s" texCoord="2"/></baseColor></pbr>)", "", variant_set)),
                "texCoord may only address uv0 and uv1");
    require(warns(make_doc(R"(<assets><asset id="t" type="texture" src="a.png"/></assets>)")),
                "a non-KTX2 asset and an orphan asset are warnings, not errors");

    // ---- §10 streams ------------------------------------------------------------------
    require(rejects(R"(<g3d version="1.0"><mesh><geometry id="g"><positions count="3">0 0 0 1 0 0 0 1 0</positions></geometry><primitive id="p" geometry="g" material="m"/></mesh><materials><material id="m"/></materials><scene><node id="root"/></scene></g3d>)"),
                "a geometry needs an indices stream");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "ASCII values must be count × components");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0 5</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "surplus ASCII values are an error");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3"></positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "a stream has neither a view nor values");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="2">0 0 0 1 0 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "positions.count must be at least 3");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><normals count="4">0 1 0 0 1 0 0 1 0 0 1 0</normals><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "every vertex stream shares positions.count");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="4">0 1 2 3</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "indices.count must be a multiple of 3");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3" componentType="float64">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "float64 is not a v1.0 componentType");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3" componentType="int32">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "int32 has no v1.0 use");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3" componentType="int16">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "positions may not be signed integers");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3" componentType="uint16">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "a quantized ASCII stream is not a thing");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices><normals count="3" normalized="true">0 1 0 0 1 0 0 1 0</normals></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")), "normalized without componentType");

    // ---- §11 morph --------------------------------------------------------------------
    require(accepts(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices>
                                   <morph count="1"><target id="smile"/><targetPositions count="3">0 0 0 0 0 0 0 0 0</targetPositions></morph></geometry>
                                   <primitive id="p2" geometry="g2" material="m" morphWeights="0"/>)")),
                "a one-target dense morph is valid");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices>
                                   <morph count="2"><target id="a"/><targetPositions count="3">0 0 0 0 0 0 0 0 0</targetPositions></morph></geometry>
                                   <primitive id="p2" geometry="g2" material="m"/>)")), "morph count must match its targets");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices>
                                   <morph count="1"><target id="a"/><targetPositions count="4">0 0 0 0</targetPositions></morph></geometry>
                                   <primitive id="p2" geometry="g2" material="m"/>)")), "dense morph storage is count × positions");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices>
                                   <morph count="1"><target id="a"/></morph></geometry>
                                   <primitive id="p2" geometry="g2" material="m"/>)")), "a morph needs targetPositions");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices>
                                   <morph count="9"><target id="a"/><targetPositions count="27">0 0 0</targetPositions></morph></geometry>
                                   <primitive id="p2" geometry="g2" material="m"/>)")), "morph count is capped at 8");

    // ---- pool addressing (BINARY.md §3–§7) --------------------------------------------
    require(accepts(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16" topology="triangles"/>)")),
                "a pool-addressed triangle is valid");
    require(rejects(pooled(R"(<positions count="3" view="v" byteOffset="0"/><indices count="3" view="vi" componentType="uint16"/>)")),
                "a pooled stream requires componentType");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32">0 0 0 1 0 0 0 1 0</positions><indices count="3" view="vi" componentType="uint16"/>)")),
                "a stream may not be a view and text at once");
    require(rejects(pooled(R"(<positions view="v" componentType="float32"/><indices view="vi" componentType="uint16"/>)")),
                "a stream requires count");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="int32"/><indices count="3" view="vi" componentType="uint16"/>)")),
                "int32 is rejected for pooled streams too");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32" byteOffset="4"/><indices count="3" view="vi" componentType="uint16"/>)")),
                "a pooled stream must fit its view");
    require(rejects(pooled(R"(<positions count="3" componentType="float32" byteOffset="4">0 0 0 1 0 0 0 1 0</positions><indices count="3">0 1 2</indices>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="65536"/>)",
                           R"(<bufferView id="v" buffer="b" byteOffset="0" byteLength="36"/>)")),
                "byteOffset means nothing in ASCII mode");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="uint16"/><indices count="3" view="vi" componentType="uint16"/>)")),
                "uint16 positions require quantization");
    require(accepts(pooled(R"(<positions count="3" view="v" componentType="uint16"><quantization offset="-1 -1 -1" scale="2 2 2" min="-1 -1 -1" max="1 1 1"/></positions><indices count="3" view="vi" componentType="uint16"/>)")),
                "a consistent quantization box is valid");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="uint16"><quantization offset="-1 -1 -1" scale="2 2 2" min="-1 -1 -1" max="2 2 2"/></positions><indices count="3" view="vi" componentType="uint16"/>)")),
                "quantization max must equal offset + scale");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="uint16"><quantization offset="-1 -1 -1" scale="2 2 2"/></positions><indices count="3" view="vi" componentType="uint16"/>)")),
                "quantization needs the box as well as the map");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="int16"><quantization offset="-1 -1 -1" scale="2 2 2" min="-1 -1 -1" max="1 1 1"/></positions><indices count="3" view="vi" componentType="uint16"/>)")),
                "a quantized stream may not be signed");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="3" byteLength="65536" align="4"/>)")),
                "a buffer offset must satisfy its own align");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="65536" align="200"/>)")),
                "buffer align must be a power of two");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="32"/>)")),
                "a view must fit its buffer");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="65536"/>)",
                           R"(<bufferView id="v" buffer="b" byteOffset="2" byteLength="36"/><bufferView id="vi" buffer="b" byteOffset="64" byteLength="6"/>)")),
                "a view offset must be a multiple of 4");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="4" byteLength="65536"/>)",
                           R"(<bufferView id="v" buffer="b" byteOffset="2" byteLength="36"/><bufferView id="vi" buffer="b" byteOffset="20" byteLength="6"/>)")),
                "view alignment is measured from the pool start, not the buffer");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="65536"/>)",
                           R"(<bufferView id="v" buffer="b" byteOffset="4" byteLength="36" target="elementArrayBuffer"/><bufferView id="vi" buffer="b" byteOffset="64" byteLength="6"/>)")),
                "elementArrayBuffer needs a 16-byte aligned offset");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="65536"/>)",
                           R"(<bufferView id="v" buffer="b" byteOffset="0" byteLength="36" target="drawIndirect"/><bufferView id="vi" buffer="b" byteOffset="64" byteLength="6"/>)")),
                "bufferView target must be arrayBuffer or elementArrayBuffer");
    require(rejects(pooled(R"(<positions count="3" view="v" componentType="float32"/><normals count="3" view="v" byteOffset="12" componentType="int8" normalized="true"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="65536"/>)",
                           R"(<bufferView id="v" buffer="b" byteOffset="0" byteLength="48" byteStride="6"/><bufferView id="vi" buffer="b" byteOffset="64" byteLength="6"/>)")),
                "byteStride must be a multiple of 4");
    require(accepts(pooled(R"(<positions count="3" view="v" byteOffset="0" componentType="float32"/><uv0 count="3" view="v" byteOffset="12" componentType="uint16" normalized="true"/><indices count="3" view="vi" componentType="uint16"/>)",
                           R"(<buffer id="b" byteOffset="0" byteLength="65536"/>)",
                           R"(<bufferView id="v" buffer="b" byteOffset="0" byteLength="60" byteStride="20"/><bufferView id="vi" buffer="b" byteOffset="64" byteLength="6" target="elementArrayBuffer"/>)")),
                "an interleaved view is laid out with byteStride");
    require(warns(pooled(R"(<positions count="3" view="v" byteOffset="1" componentType="float32"/><indices count="3" view="vi" componentType="uint16"/>)",
                         R"(<buffer id="b" byteOffset="0" byteLength="65536"/>)",
                         R"(<bufferView id="v" buffer="b" byteOffset="0" byteLength="48"/><bufferView id="vi" buffer="b" byteOffset="64" byteLength="6"/>)")),
                "a component boundary inside a stride is a warning");

    // ---- §9 skeletons -----------------------------------------------------------------
    require(accepts(make_doc(rig, "", "", R"(<node id="root"><node id="a"/><node id="b"/></node>)")),
                "a two-joint skeleton with identity bind matrices is valid");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a"><joint id="a" parent=""/><joint id="b" parent="a"/></skeleton></skeletons>)",
                             "", "", R"(<node id="root"><node id="a"/><node id="b"/></node>)")),
                "every joint must be addressable through nodes");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a ghost"><joint id="a" parent=""/></skeleton></skeletons>)",
                             "", "", R"(<node id="root"><node id="a"/></node>)")),
                "nodes may only name declared joints");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a b"><joint id="a" parent="b"/><joint id="b" parent="a"/></skeleton></skeletons>)",
                             "", "", R"(<node id="root"><node id="a"/><node id="b"/></node>)")),
                "a joint cycle is an error, not a hang");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a b"><joint id="a" parent=""/><joint id="b" parent=""/></skeleton></skeletons>)",
                             "", "", R"(<node id="root"><node id="a"/><node id="b"/></node>)")),
                "the joint graph is single-root");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a"><joint id="a" parent=""/>
                               <inverseBindMatrices count="1">1 0 0 0</inverseBindMatrices></skeleton></skeletons>)",
                             "", "", R"(<node id="root"><node id="a"/></node>)")),
                "one 4x4 matrix per joint");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a"><joint id="a" parent=""/>
                               <inverseBindMatrices count="1" componentType="int16">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</inverseBindMatrices></skeleton></skeletons>)",
                             "", "", R"(<node id="root"><node id="a"/></node>)")),
                "inverse bind matrices are float32");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a"><joint id="a" parent=""/></skeleton>
                               <skin id="sk" skeleton="rig" maxInfluences="8"/></skeletons>)")),
                "maxInfluences is 1, 2 or 4 in v1.0");
    require(rejects(make_doc(R"(<skeletons><skeleton id="rig" nodes="a"><joint id="a" parent=""/></skeleton>
                               <skin id="sk" skeleton="rig"><bind node="a" mode="dualQuaternion"/></skin></skeletons>)")),
                "dualQuaternion is an extension, not a v1.0 bind mode");
    require(rejects(make_doc(rig, R"(<primitive id="p2" geometry="g" material="m" skin="sk"/>)")),
                "a skinned primitive needs joints0 and weights0");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions>)" + std::string(skin_streams) +
                                 R"(<indices count="3">0 1 2</indices></geometry><primitive id="p2" geometry="g2" material="m"/>)", rig)),
                "skin streams without a skin");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="3">0 0 0 1 0 0 0 1 0</positions><joints0 count="3" normalized="true">0 0 0 0 0 0 0 0 0 0 0 0</joints0>)" +
                                 std::string(skin_streams) + R"(<indices count="3">0 1 2</indices></geometry><primitive id="p2" geometry="g2" material="m" skin="sk"/>)", rig)),
                "joints0 indices are never normalised");

    // ---- §12 materials ----------------------------------------------------------------
    require(accepts(material_doc(R"(<pbr workflow="metallicRoughness"><baseColor r="1" g="1" b="1" a="1"/></pbr>)")),
                "an explicit workflow with a full base colour is valid");
    require(rejects(material_doc(R"(<pbr workflow="specularGlossiness"/>)")), "specularGlossiness is an extension candidate");
    require(rejects(material_doc(R"(<pbr><metallic value="2"/></pbr>)")), "metallic must be in 0..1");
    require(rejects(material_doc(R"(<pbr><baseColor r="0.5" g="0,5" b="0.5"/></pbr>)")), "a comma is not a decimal separator");
    require(rejects(material_doc(R"(<pbr><emissive r="0" g="0" b="0" strength="-1"/></pbr>)")), "emissive strength must be >= 0");
    require(rejects(material_doc("", R"( alphaMode="mask")")), "alphaMode mask requires alphaCutoff");
    require(rejects(material_doc("", R"( alphaMode="fade")")), "alphaMode must be opaque, mask or blend");
    require(rejects(material_doc("", R"( alphaMode="mask" alphaCutoff="+0.5")")), "alphaCutoff must be a plain number");
    require(accepts(material_doc("", R"( alphaMode="mask" alphaCutoff="0.5" doubleSided="true" premultiplied="false")")),
                "a masked double-sided material is valid");
    require(rejects(material_doc("", R"( doubleSided="yes")")), "a boolean attribute is true/false/1/0");

    // ---- §13 cameras and lights -------------------------------------------------------
    require(accepts(make_doc("", "", R"(<cameras><camera id="c" type="orthographic"><xmag value="2"/><ymag value="1.5"/></camera></cameras>)")),
                "an orthographic camera needs magnification");
    require(rejects(make_doc("", "", R"(<cameras><camera id="c" type="perspective"><yfov value="0.7"/><znear value="1"/><zfar value="0.5"/></camera></cameras>)")),
                "zfar must exceed znear");
    require(rejects(make_doc("", "", R"(<cameras><camera id="c" type="perspective"><yfov value="0.7"/><znear value="0.1"/><zfar value="10"/><xmag value="2"/></camera></cameras>)")),
                "the two projections are exclusive");
    require(rejects(make_doc("", "", R"(<cameras><camera id="c" type="fisheye"><yfov value="0.7"/></camera></cameras>)")),
                "camera type must be perspective or orthographic");
    require(rejects(make_doc("", "", R"(<lights><light id="l" type="directional"><color r="1" g="1" b="1"/><intensity value="1"/><range value="10"/></light></lights>)")),
                "a directional light has no range");
    require(rejects(make_doc("", "", R"(<lights><light id="l" type="point"><color r="1" g="1" b="2"/><intensity value="1"/></light></lights>)")),
                "light colour components are 0..1");
    require(rejects(make_doc("", "", R"(<lights><light id="l" type="point"><color r="1" g="1" b="1"/><intensity value="1"/><outerConeAngle value="0.4"/></light></lights>)")),
                "cone angles belong to a spot light");
    require(rejects(make_doc("", "", R"(<lights><light id="l" type="spot"><color r="1" g="1" b="1"/><intensity value="1"/>
                                        <innerConeAngle value="0.6"/><outerConeAngle value="0.4"/></light></lights>)")),
                "innerConeAngle may not exceed outerConeAngle");

    // ---- §14 animations ---------------------------------------------------------------
    require(accepts(make_doc("", "", R"(<animations><animation id="a" duration="2"><sampler id="sa">
                                       <input count="2">0 2</input><output count="2">0 0 0 1 0 0 0 1</output></sampler>
                                       <channel target="root" property="rotation" sampler="sa" interpolation="step"/></animation></animations>)")),
                "a rotation channel with two keyframes is valid");
    require(rejects(make_doc("", "", R"(<animations><animation id="a"><sampler id="sa"><input count="2">0 2</input>
                                       <output count="2">0 0 0 1 0 0 0 1</output></sampler></animation></animations>)")),
                "an animation requires duration");
    require(rejects(make_doc("", "", R"(<animations><animation id="a" duration="0"><sampler id="sa"><input count="2">0 2</input>
                                       <output count="2">0 0 0 1 0 0 0 1</output></sampler></animation></animations>)")),
                "duration must be positive");
    require(rejects(make_doc("", "", R"(<animations><animation id="a" duration="1"><sampler id="sa"><input count="2">0 2</input>
                                       <output count="3">0 0 0 1 0 0 0 1 0 0 0 1</output></sampler></animation></animations>)")),
                "input and output share a keyframe count");
    require(rejects(make_doc("", "", R"(<animations><animation id="a" duration="1"><sampler id="sa"><input count="2">0 2</input></sampler></animation></animations>)")),
                "a sampler requires both streams");
    require(rejects(make_doc("", "", R"(<animations><animation id="a" duration="1"><sampler id="sa"><input count="2">0 2</input>
                                       <output count="2">0 0 0 1 0 0 0 1</output></sampler>
                                       <channel target="root" property="shear" sampler="sa"/></animation></animations>)")),
                "shear is not an animatable property");
    require(rejects(make_doc("", "", R"(<animations><animation id="a" duration="1"><sampler id="sa"><input count="2">0 2</input>
                                       <output count="2">0 0 0 1 0 0 0 1</output></sampler>
                                       <channel target="root" property="rotation" sampler="sa" interpolation="cubic"/></animation></animations>)")),
                "cubic interpolation is not supported");
    require(rejects(make_doc("", "", R"(<animations><animation id="a" duration="1"><sampler id="sa"><input count="2">0 2</input>
                                       <output count="2">0 0 0 1 0 0 0 1</output></sampler>
                                       <channel target="root" property="rotation" sampler="sa"/>
                                       <channel target="root" property="rotation" sampler="sa"/></animation></animations>)")),
                "one channel per target and property");

    // ---- §15 collisions ---------------------------------------------------------------
    require(accepts(make_doc("", "", R"(<collisions><body id="c" node="root" type="dynamic"><shape type="sphere" radius="0.5"/></body></collisions>)")),
                "a sphere body on the root node is valid");
    require(rejects(make_doc("", "", R"(<collisions><body id="c" node="root" type="dynamic"><shape type="sphere"/></body></collisions>)")),
                "a sphere shape requires a radius");
    require(rejects(make_doc("", "", R"(<collisions><body id="c" node="root" type="dynamic"><shape type="capsule" radius="0.5" height="0"/></body></collisions>)")),
                "capsule height must be positive");
    require(rejects(make_doc("", "", R"(<collisions><body id="c" node="root" type="dynamic"><shape type="box" extent="1 2"/></body></collisions>)")),
                "a box extent is three numbers");
    require(rejects(make_doc("", "", R"(<collisions><body id="c" node="root" type="dynamic"><shape type="mesh" geometry="g" kind="staticTriangleMesh"/></body></collisions>)")),
                "staticTriangleMesh requires a static body");
    require(rejects(make_doc("", "", R"(<collisions><body id="c" node="ghost" type="static"><shape type="sphere" radius="1"/></body></collisions>)")),
                "a body must name a node");
    require(rejects(make_doc("", "", R"(<collisions><body id="c" node="root" type="kinematic"><shape type="sphere" radius="1" scale="2"/></body></collisions>)")),
                "shape-local scale is unsupported");
    require(rejects(make_doc("", "", R"(<collisions><body id="c" node="root" type="sleeping"><shape type="sphere" radius="1"/></body></collisions>)")),
                "body type must be static, dynamic or kinematic");

    // ---- §16 scene and §17 limits -----------------------------------------------------
    require(accepts(make_doc("", "", "", R"(<node id="root"><node id="child"><mesh ref="p"/></node></node>)")),
                "nesting is by containment");
    require(rejects(make_doc("", "",
                             R"(<cameras><camera id="ca" type="orthographic"><xmag value="1"/><ymag value="1"/></camera>
                                <camera id="cb" type="orthographic"><xmag value="1"/><ymag value="1"/></camera></cameras>)",
                             R"(<node id="root"><camera ref="ca"/><camera ref="cb"/></node>)")),
                "at most one camera per node");
    require(rejects(geometry_doc(R"(<geometry id="g2"><positions count="16777216">0 0 0</positions><indices count="3">0 1 2</indices></geometry>
                                  <primitive id="p2" geometry="g2" material="m"/>)")),
                "positions.count is capped at the 24-bit ceiling");
    require(rejects(make_doc(R"(<assets><textureSet id="s">
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*" default="true"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                <image assetId="t" vkFormat="VK_FORMAT_R8G8B8A8_SRGB" when="*"/>
                                </textureSet><asset id="t" type="texture" src="a.ktx2"/></assets>)")),
                "a textureSet holds at most eight variants");

    // ---- repository samples -----------------------------------------------------------
#ifdef G3D_SAMPLES_DIR
    const std::filesystem::path samples = G3D_SAMPLES_DIR;
    for (const char* name : {"v1/character.g3d", "primitives/triangle.g3d", "primitives/square.g3d",
                             "primitives/sphere.g3d", "primitives/cylinder.g3d", "primitives/torus.g3d",
                             "primitives/flat.g3d"}) {
        const auto parsed = g3d::load(samples / name);
        require(parsed.valid(), std::string("repository sample parses: ") + name);
        if (!parsed.valid())
            for (const auto& d : parsed.diagnostics) std::printf("  %s:%zu: %s\n", name, d.line, d.message.c_str());
    }
#endif

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("all checks passed\n");
    return 0;
}
