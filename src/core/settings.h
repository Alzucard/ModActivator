#pragma once
namespace ml { struct Config { bool wrapSwapChain=false, enableDred=false, hookXInput=true; }; namespace Settings { inline Config& Get(){static Config c;return c;} } }
