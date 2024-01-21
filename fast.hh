#ifndef __FTL_FAST__
#define __FTL_FAST__

#include <cinttypes>
#include <unordered_map>
#include <vector>

#include "ftl/abstract_ftl.hh"
#include "ftl/common/block.hh"
#include "ftl/ftl.hh"
#include "pal/pal.hh"

namespace SimpleSSD {

namespace FTL {

class FAST : public AbstractFTL {
  private:
    PAL::PAL *pPAL;

    ConfigReader &conf;

    // Contains all the mapping from logical block to physical data block
    std::unordered_map<uint32_t, uint32_t> block_table;
    // Contains all the mapping from logical page to physical page in RW blocks
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> page_table;

    std::unordered_map<uint32_t, Block> blocks;
    std::list<std::pair<uint32_t, Block>> freeBlocks;
    std::vector<std::pair<uint32_t, Block*>> RWBlocks;

    struct SW_Block_Info {
      // The physical block index of the SW block
      uint32_t block_idx;
      // The physical block index the SW block belongs to
      uint32_t belong_data_block_physical_idx;
      // The logical block index the SW block belongs to
      uint32_t belong_data_block_logical_idx;

      uint32_t cur_page_idx;
      uint32_t res_page_cnt;
      uint32_t last_access_ppi;
    };
    SW_Block_Info* sw_info;

    bool bRandomTweak;

    void calculateTotalPages(uint64_t&, uint64_t&);

    void readInternal(Request&, uint64_t&);
    void writeInternal(Request&, uint64_t&, bool = true);

    uint32_t allocateFreeBlock();
    void pageRead(uint32_t, uint32_t, Request&, uint64_t&);
    void pageWrite(uint32_t, uint32_t, uint32_t, uint32_t, Request&, bool, bool, uint64_t&);
    void blockErase(uint32_t, uint64_t&);
    void initSWBlockInfo(uint32_t, uint32_t, uint32_t);
    std::pair<uint32_t, uint32_t> allocateRWPage(uint64_t&);

    void mergeSwitch(uint64_t&);
    void mergePartial(uint64_t&);

    inline uint64_t calcPageNumber(uint32_t, uint32_t);
    void ReadAndWrite(uint32_t, Block*, uint32_t, Block*, uint32_t, uint32_t, uint64_t&);

    bool invalidRWBlockPage(uint64_t, uint32_t&, uint32_t&);

  public:
    FAST(ConfigReader &, Parameter &, PAL::PAL *, DRAM::AbstractDRAM *);
    ~FAST();

    bool initialize() override;

    void read(Request &, uint64_t &) override;
    void write(Request &, uint64_t &) override;

    void trim(Request &, uint64_t &) override;
    void format(LPNRange &, uint64_t &) override;
    Status *getStatus(uint64_t, uint64_t) override;
};

}

}

#endif
