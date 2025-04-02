# TODO

* Test allocation limit support
* Test reference + allocation limit (can decode larger with ref)
* Decoding fuzz testing
* Round trip fuzz testing
* Differential fuzz testing
    * We encode, other decodes
    * Other encodes (subset of what we support), we decode
    * Both decode garbage, ensure we match