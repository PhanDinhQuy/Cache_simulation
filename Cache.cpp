#include <iostream>
#include <vector>
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include <cmath>

using namespace std;

const int CACHE_LINE_SIZE = 64; // Kích thước mỗi dòng cache (byte)
const int L1I_SETS = 16384;    // Số tập hợp trong L1 Instruction Cache
const int L1D_SETS = 16384;    // Số tập hợp trong L1 Data Cache
const int L1I_ASSOC = 2;       // Associativity của L1 Instruction Cache
const int L1D_ASSOC = 4;       // Associativity của L1 Data Cache

enum CacheOperation { READ = 0, WRITE = 1, FETCH = 2, EVICT = 3, RESET = 8, PRINT = 9 };

struct CacheLine {
    bool valid = false;       // Cho biết dòng có hợp lệ không
    bool dirty = false;       // Đánh dấu dòng đã bị ghi (Modified)
    uint32_t tag = 0;         // Phần `tag` của địa chỉ
    int lru = 0;              // Bộ đếm LRU
    int writeCount = 0;       // Đếm số lần ghi vào dòng (phân biệt các giai đoạn)
};

class Cache {
private:
    int numSets;
    int associativity;
    bool verbose;
    vector<vector<CacheLine>> cache;
    int cacheReads = 0, cacheWrites = 0;
    int cacheHits = 0, cacheMisses = 0;

    void communicateWithL2(const string &operation, uint32_t address) {
        if (verbose) {
            cout << operation << " 0x" << hex << address << endl;
        }
    }

    int log2(int value) {
        return (int)log2f(value);
    }

    string getState(const CacheLine &line) const {
        if (!line.valid) return "I"; // Invalid
        return line.writeCount >= 2 ? "M" : "V"; // Modified hoặc Valid
    }

public:
    Cache(int sets, int assoc, bool verbosity)
        : numSets(sets), associativity(assoc), verbose(verbosity) {
        cache.resize(numSets, vector<CacheLine>(associativity));
    }
void evictLine(uint32_t address) {
        int offsetBits = log2(CACHE_LINE_SIZE);
        int indexBits = log2(numSets);
        

        uint32_t setIndex = (address >> offsetBits) & ((1 << indexBits) - 1); // Extract set index
        uint32_t tag = address >> (offsetBits + indexBits);                  // Extract tag
        int evictedLRU = -1;
        // Search for the line to evict in the set
        for (int i = 0; i < associativity; ++i) {
            if (cache[setIndex][i].valid && cache[setIndex][i].tag == tag) {
                
                evictedLRU = cache[setIndex][i].lru;
                
                // Clear the evicted cache line
                cache[setIndex][i] = CacheLine(); // Reset to default
                if (evictedLRU != -1) {
                    // Adjust LRU values for remaining lines
                    for (int j = 0; j < associativity; ++j) {
                        if (cache[setIndex][j].valid && cache[setIndex][j].lru > evictedLRU) {
                            cache[setIndex][j].lru--;
                        }
                    }

                    break; // Evict only one line
                }
            } 
        }
    }
    void access(uint32_t address, CacheOperation operation) {
        int offsetBits = log2(CACHE_LINE_SIZE);
        int indexBits = log2(numSets);

        uint32_t setIndex = (address >> offsetBits) & ((1 << indexBits) - 1);
        uint32_t tag = address >> (offsetBits + indexBits);

        bool hit = false;
        int lruIndex = -1;
        if (operation == READ || operation == FETCH) {
            cacheReads++; // Increment cache read counter
        } else if (operation == WRITE) {
            cacheWrites++; // Increment cache write counter
        }
        if (operation == EVICT) {
            evictLine(address);
            return; // No further processing needed
        }
        // Kiểm tra cache hit
        for (int i = 0; i < associativity; ++i) {
            auto &line = cache[setIndex][i];
            if (line.valid && line.tag == tag) {
                hit = true;
                line.lru = 0; // Cập nhật LRU cho dòng vừa truy cập
                cacheHits++;
                for (int j = 0; j < associativity; ++j) {
                    if (j != i && cache[setIndex][j].valid) {
                        cache[setIndex][j].lru++;
                    }
                }

                if (operation == WRITE) {
                    line.writeCount++; // Tăng số lần ghi
                    if (line.writeCount == 1) {
                    } else if (line.writeCount == 2) {
                        // Lần ghi thứ hai: Trạng thái Modified, bật dirty
                        line.dirty = true;
                    } else {
                        communicateWithL2("Write to L2", address);
                    }
                }
                return;
            }
        }

        // Nếu cache miss
        cacheMisses++;
        if (operation == READ || operation == FETCH) {
            communicateWithL2("Read from L2", address);
        } else if (operation == WRITE) {
            communicateWithL2("Read for Ownership from L2", address);
        }

        // Evict dòng cũ và thêm dòng mới
        int maxLRU = -1;
        for (int i = 0; i < associativity; ++i) {
            if (!cache[setIndex][i].valid) {
                lruIndex = i;
                break;
            }
            if (cache[setIndex][i].lru > maxLRU) {
                maxLRU = cache[setIndex][i].lru;
                lruIndex = i;
            }
        }

  // Thay thế dòng mới
        auto &newLine = cache[setIndex][lruIndex];
        newLine.valid = true;
        newLine.tag = tag;
        newLine.lru = 0;
        newLine.dirty = false; // Không bật dirty khi mới thêm vào
        newLine.writeCount = (operation == WRITE) ? 1 : 0; // Bắt đầu từ lần ghi đầu tiên

        // Cập nhật LRU cho các dòng khác
        for (int i = 0; i < associativity; ++i) {
            if (i != lruIndex && cache[setIndex][i].valid) {
                cache[setIndex][i].lru++;
            }
        }
    }

    void reset() {
        for (auto &set : cache) {
            for (auto &line : set) {
                line = CacheLine();
            }
        }
        cacheReads = cacheWrites = cacheHits = cacheMisses = 0;
    }

    void printStats() const {
        cout << "Number of cache reads: " << cacheReads << endl;
        cout << "Number of cache writes: " << cacheWrites << endl;
        cout << "Number of cache hits: " << cacheHits << endl;
        cout << "Number of cache misses: " << cacheMisses << endl;
        cout << "Cache hit ratio: " << (cacheHits / static_cast<float>(cacheHits + cacheMisses)) << endl;
    }

    void printContents() const {
        for (size_t i = 0; i < cache.size(); ++i) {
            stringstream setOutput;
            bool setHasValidLines = false;

            for (size_t way = 0; way < cache[i].size(); ++way) {
                const auto &line = cache[i][way];
                if (line.valid) {
                    setHasValidLines = true;
                    string state = getState(line);
                    setOutput << "  Way " << way 
                              << ": [Tag: 0x" << hex << line.tag
                              << ", State: " << state
                              << ", Dirty: " << line.dirty
                              << ", LRU: " << line.lru << "]" << endl;
                }
            }

            if (setHasValidLines) {
                cout << "Set " << dec << i << ":" << endl << setOutput.str();
            }
        }
    }
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <mode> <trace_file>" << endl;
        return 1;
    }

    int mode = stoi(argv[1]);
    string traceFile = argv[2];
    bool verbose = (mode == 1);

    Cache l1InstructionCache(L1I_SETS, L1I_ASSOC, verbose);
    Cache l1DataCache(L1D_SETS, L1D_ASSOC, verbose);

    ifstream infile(traceFile);
    if (!infile.is_open()) {
        cerr << "Error opening trace file!" << endl;
        return 1;
    }

    string line;
    while (getline(infile, line)) {
        istringstream iss(line);
        int operation;
        uint32_t address = 0;

        if (!(iss >> operation)) continue;

        if (operation == READ || operation == WRITE || operation == FETCH || operation == EVICT) {
            if (!(iss >> hex >> address)) {
                cerr << "Error: Address missing for operation " << operation << endl;
                continue;
            }
        }

        switch (operation) {
            case READ:
                //cout << "\nAccessing Address: 0x" << hex << address << " | Operation: READ " << endl;
                l1DataCache.access(address, READ);
                break;
            case WRITE:
                //cout << "\nAccessing Address: 0x" << hex << address << " | Operation: WRITE " << endl;
                l1DataCache.access(address, WRITE);
                break;
            case FETCH:
                //cout << "\nAccessing Address: 0x" << hex << address << " | Operation: FETCH " << endl;
                l1InstructionCache.access(address, FETCH);
                break;
            case EVICT:
                //cout << "\nAccessing Address: 0x" << hex << address << " | Operation: EVICT " << endl;
                l1DataCache.access(address, EVICT);
                l1InstructionCache.access(address, EVICT);
                cout << "Eviction requested for address: 0x" << hex << address << endl;
                break;
            case RESET:
                cout << "Resetting caches..." << endl;
                l1InstructionCache.reset();
                l1DataCache.reset();
                break;
            case PRINT:
                cout << "Instruction Cache Contents:" << endl;
                l1InstructionCache.printContents();
                cout << "Data Cache Contents:" << endl;
                l1DataCache.printContents();
                break;
            default:
                cerr << "Unknown operation: " << operation << endl;
                break;
        }
    }

    cout << "Instruction Cache Statistics:" << endl;
    l1InstructionCache.printStats();

    cout << "Data Cache Statistics:" << endl;
    l1DataCache.printStats();

    return 0;
}
