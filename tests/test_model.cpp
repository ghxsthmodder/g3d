#include "g3d/model.hpp"

#include <cassert>
#include <string>

int main() {
    const std::string valid = R"(<?g3d version="1.0.0" encoding="utf-8"?>
<model>
  <assets><asset id="1" type="texture" file="albedo.png"/></assets>
  <materials><material id="0"><diffuse assetId="1"/></material></materials>
  <mesh externalFile="mesh.g3dmesh"/>
</model>)";
    const auto doc = g3d::parse(valid);
    assert(doc.valid());
    assert(doc.version == "1.0.0");
    assert(doc.assets.size() == 1);
    assert(doc.external_files.size() == 1);

    const std::string duplicate = R"(<model><assets>
      <asset id="1" type="texture"/><asset id="1" type="shader"/>
    </assets></model>)";
    assert(!g3d::parse(duplicate).valid());

    const std::string missing_ref = R"(<model><materials><material><diffuse assetId="404"/></material></materials></model>)";
    assert(!g3d::parse(missing_ref).valid());

    const std::string malformed = R"(<model><assets></model>)";
    assert(!g3d::parse(malformed).valid());
    return 0;
}
