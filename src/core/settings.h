#pragma once
namespace ml { struct Config { bool wrapSwapChain=false, enableDred=false, hookXInput=true, debugInput=false; }; namespace Settings { inline Config& Get(){static Config c;return c;} } }
