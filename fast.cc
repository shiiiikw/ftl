#include "ftl/fast.hh"

#include <algorithm>
#include <limits>
#include <random>

#include "util/algorithm.hh"
#include "util/bitset.hh"

namespace SimpleSSD {

namespace FTL {

#define USE_IT(x) (void)x;

#define PROGRAM_DEBUG_FLAG false
bool DEBUG_FLAG = false;
#define DEBUG_TEST_START {if (PROGRAM_DEBUG_FLAG) DEBUG_FLAG = true;}
#define DEBUG_TEST_END {DEBUG_FLAG = false;}
#define DEBUG_TEST(x) {if (DEBUG_FLAG) printf("Debug point: %s\n", x);}
#define DEBUG_TEST_TICK(x) {if (DEBUG_FLAG) printf("Current tick: %lu\n", x);}

#define PARA_NUM 128

constexpr uint64_t SW_BLOCK_NUM = 1;
constexpr uint64_t RW_BLOCK_NUM = 6;

FAST::FAST(ConfigReader &c, Parameter &p, PAL::PAL *l, DRAM::AbstractDRAM *d) :
    AbstractFTL(p, l, d),
    pPAL(l),
    conf(c)
{
    blocks.reserve(param.totalPhysicalBlocks);
    block_table.reserve(param.totalLogicalBlocks);
    page_table.reserve(param.totalLogicalBlocks * param.pagesInBlock);

    if (param.totalPhysicalBlocks < SW_BLOCK_NUM + RW_BLOCK_NUM)
        panic("In FAST initialization: the number of free block if too small!\n");

    for (uint32_t i = 0; i < param.totalPhysicalBlocks; i++) {
        if (i < RW_BLOCK_NUM) {
            blocks.emplace(i, Block(i, param.pagesInBlock, param.ioUnitInPage));
            auto iter = blocks.find(i);
            RWBlocks.push_back(make_pair(i, &iter->second));
        }
        else
            freeBlocks.push_back(make_pair(i, Block(i, param.pagesInBlock, param.ioUnitInPage)));
    }

    sw_info = nullptr;

    bRandomTweak = conf.readBoolean(CONFIG_FTL, FTL_USE_RANDOM_IO_TWEAK);
}
FAST::~FAST() {}

void FAST::calculateTotalPages(uint64_t& valid, uint64_t& invalid) {
    valid = 0;
    invalid = 0;

    for (auto &iter : blocks) {
        valid += iter.second.getValidPageCount();
        invalid += iter.second.getDirtyPageCount();
    }
}

bool FAST::initialize() {
    uint64_t nPagesToWarmup;
    uint64_t nPagesToInvalidate;
    uint64_t nTotalLogicalPages;
    uint64_t maxPagesBeforeGC;
    uint64_t tick;
    uint64_t valid;
    uint64_t invalid;
    FILLING_MODE mode;

    Request req(param.ioUnitInPage);



    nTotalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;
    nPagesToWarmup =
        nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_FILL_RATIO);
    nPagesToInvalidate =
        nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_INVALID_PAGE_RATIO);
    mode = (FILLING_MODE)conf.readUint(CONFIG_FTL, FTL_FILLING_MODE);
    maxPagesBeforeGC =
        param.pagesInBlock *
        (param.totalPhysicalBlocks *
            (1 - conf.readFloat(CONFIG_FTL, FTL_GC_THRESHOLD_RATIO)) -
        param.pageCountToMaxPerf);  // # free blocks to maintain

    if (nPagesToWarmup + nPagesToInvalidate > maxPagesBeforeGC) {
        warn("ftl: Too high filling ratio. Adjusting invalidPageRatio.");
        nPagesToInvalidate = maxPagesBeforeGC - nPagesToWarmup;
    }



    req.ioFlag.set();

    // Step 1. Filling
    if (mode == FILLING_MODE_0 || mode == FILLING_MODE_1) {
        // Sequential
        for (uint64_t i = 0; i < nPagesToWarmup; i++) {
        tick = 0;
        req.lpn = i;
        writeInternal(req, tick, false);
        }
    }
    else {
        // Random
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

        for (uint64_t i = 0; i < nPagesToWarmup; i++) {
        tick = 0;
        req.lpn = dist(gen);
        writeInternal(req, tick, false);
        }
    }

    // Step 2. Invalidating
    if (mode == FILLING_MODE_0) {
        // Sequential
        for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
        tick = 0;
        req.lpn = i;
        writeInternal(req, tick, false);
        }
    }
    else if (mode == FILLING_MODE_1) {
        // Random
        // We can successfully restrict range of LPN to create exact number of
        // invalid pages because we wrote in sequential mannor in step 1.
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist(0, nPagesToWarmup - 1);

        for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
        tick = 0;
        req.lpn = dist(gen);
        writeInternal(req, tick, false);
        }
    }
    else {
        // Random
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

        for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
        tick = 0;
        req.lpn = dist(gen);
        writeInternal(req, tick, false);
        }
    }

    // Report
    calculateTotalPages(valid, invalid);

    return true;
}

void FAST::read(Request &req, uint64_t &tick) {
//    uint64_t begin = tick;

    if (req.ioFlag.count() > 0) {
        readInternal(req, tick);

    }
    else {
        warn("FTL got empty request");
    }

    // tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ);
}

void FAST::write(Request &req, uint64_t &tick) {

    DEBUG_TEST_START;
    DEBUG_TEST("1");

    if (req.ioFlag.count() > 0) {
        DEBUG_TEST("2");
        writeInternal(req, tick);

    }
    else {
        warn("FTL got empty request");
    }

    // tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE);
}

void FAST::trim(Request& req, uint64_t& tick) {
    USE_IT(req);
    USE_IT(tick);

    panic("Function trim is called!\n");
}

void FAST::format(LPNRange& range, uint64_t& tick) {
    USE_IT(range);
    USE_IT(tick);
    panic("Function format is called!\n");
}

Status* FAST::getStatus(uint64_t lpnBegin, uint64_t lpnEnd) {
    USE_IT(lpnBegin);
    USE_IT(lpnEnd);
    panic("Function getStatus is called!\n");
    return nullptr;
}

inline uint64_t FAST::calcPageNumber(uint32_t block_idx, uint32_t page_idx) {
    return block_idx * param.pagesInBlock + page_idx;
}

void FAST::ReadAndWrite(
    uint32_t lbn,
    Block* src_block, uint32_t src_block_idx,
    Block* dst_block, uint32_t dst_block_idx,
    uint32_t page_idx, uint64_t& tick)
{
    PAL::Request req(param.ioUnitInPage);
    // Read page from src block
    src_block->read(page_idx, 0, tick);

    req.blockIndex = src_block_idx;
    req.pageIndex = page_idx;
    if (bRandomTweak) {
        req.ioFlag.reset();
        req.ioFlag.set(0);
    }
    else {
        req.ioFlag.set();
    }
    pPAL->read(req, tick);

    // Write page to dst block
    uint64_t lpn = calcPageNumber(lbn, page_idx);
    dst_block->write(page_idx, lpn, 0, tick);

    req.blockIndex = dst_block_idx;
    req.pageIndex = page_idx;
    if (bRandomTweak) {
        req.ioFlag.reset();
        req.ioFlag.set(0);
    }
    else {
        req.ioFlag.set();
    }

    pPAL->write(req, tick);
}

uint32_t FAST::allocateFreeBlock() {
    DEBUG_TEST("Free Block Allocation");
    if (!freeBlocks.empty()) {
        DEBUG_TEST("Branch 1");

        auto iter = freeBlocks.begin();
        uint32_t pbn = iter->first;
        blocks.emplace(pbn, std::move(iter->second));

        freeBlocks.erase(iter);
        return pbn;
    }
    else {
        DEBUG_TEST("Branch 2");

        panic("Free block allocation error!\n");
        return 0;
    }
}

void FAST::pageRead(uint32_t pbn, uint32_t ppi, Request& req, uint64_t& tick) {
    PAL::Request palRequest(req);
    std::unordered_map<uint32_t, Block>::iterator block;
    uint64_t beginAt;
    uint64_t finishedAt = tick;

    if (pbn < param.totalPhysicalBlocks && ppi < param.pagesInBlock) {
        block = blocks.find(pbn);
        if (block == blocks.end())
            return;
        if (!block->second.checkValid(ppi, 0))
            return;

        palRequest.blockIndex = pbn;
        palRequest.pageIndex = ppi;

        if (bRandomTweak) {
            palRequest.ioFlag.reset();
            palRequest.ioFlag.set(0);
        }
        else {
            palRequest.ioFlag.set();
        }

        beginAt = tick;

        block->second.read(palRequest.pageIndex, 0, beginAt);
        pPAL->read(palRequest, beginAt);

        finishedAt = MAX(finishedAt, beginAt);

        tick = finishedAt;
    }
}

void FAST::pageWrite(uint32_t src_pbn, uint32_t src_ppi, uint32_t dst_pbn, uint32_t dst_ppi,
                     Request& req, bool readBeforeWrite, bool sendToPAL, uint64_t& tick)
{
    PAL::Request palRequest(req);

    uint64_t beginAt;
    uint64_t finishedAt = tick;

    std::unordered_map<uint32_t, Block>::iterator block;
    block = blocks.find(dst_pbn);

    if (block == blocks.end()) {
        panic("In page write function: destination block not found!\n");
    }

    if (req.ioFlag.test(0) || !bRandomTweak) {
        beginAt = tick;

        block->second.write(dst_ppi, req.lpn, 0, beginAt);

        if (readBeforeWrite && sendToPAL) {
            palRequest.blockIndex = src_pbn;
            palRequest.pageIndex = src_ppi;

            palRequest.ioFlag = req.ioFlag;
            palRequest.ioFlag.flip();

            pPAL->read(palRequest, beginAt);
        }

        if (sendToPAL) {
            palRequest.blockIndex = dst_pbn;
            palRequest.pageIndex = dst_ppi;

            if (bRandomTweak) {
                palRequest.ioFlag.reset();
                palRequest.ioFlag.set(0);
            }
            else {
                palRequest.ioFlag.set();
            }

            pPAL->write(palRequest, beginAt);
        }

        finishedAt = MAX(finishedAt, beginAt);
    }

    if (sendToPAL) {
        tick = finishedAt;
        // tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE_INTERNAL);
    }
}

void FAST::blockErase(uint32_t pbn, uint64_t& tick) {
    std::unordered_map<uint32_t, Block>::iterator block;
    block = blocks.find(pbn);
    block->second.erase();

    PAL::Request req(param.ioUnitInPage);
    req.blockIndex = block->first;
    req.pageIndex = 0;
    req.ioFlag.set();
    pPAL->erase(req, tick);

    freeBlocks.push_back(make_pair(pbn, std::move(block->second)));
    blocks.erase(block);
}

void FAST::initSWBlockInfo(uint32_t idx, uint32_t be_p_idx, uint32_t be_l_idx) {
    if (sw_info != nullptr)
        panic("SW block has already existed!\n");

    sw_info = new SW_Block_Info;

    sw_info->block_idx = idx;
    sw_info->belong_data_block_physical_idx = be_p_idx;
    sw_info->belong_data_block_logical_idx = be_l_idx;

    sw_info->cur_page_idx = 0;
    sw_info->res_page_cnt = param.pagesInBlock;
}

std::pair<uint32_t, uint32_t> FAST::allocateRWPage(uint64_t& tick) {
    bool clean_page_found = false;
    uint32_t dst_pbn, dst_ppi;
    for (int i=0; i<(int)RWBlocks.size(); i++) {
        auto info_pair = RWBlocks[i];
        uint32_t pbn = info_pair.first;
        Block* rw_block = info_pair.second;

        uint32_t next_write_page_idx = rw_block->getNextWritePageIndex();
        if (next_write_page_idx < param.pagesInBlock) {
            clean_page_found = true;
            dst_pbn = pbn;
            dst_ppi = next_write_page_idx;
            break;
        }
    }

    // Trigger GC event
    if (!clean_page_found) {
//        debugprint(LOG_FTL_FAST, "GC is triggered");

        // Erase the first rw block
        /* All the mappings from lbn to old data block */
        static std::unordered_map<uint32_t, std::pair<uint32_t, Block*>> tool_src_table;
        /* All the mappings from lbn to new data block */
        static std::unordered_map<uint32_t, std::pair<uint32_t, Block*>> tool_dst_table;
        static std::vector<uint64_t> tool_mapping_update_list;
        auto info_pair = RWBlocks[0];
        uint32_t victim_idx = info_pair.first;
        // Block* victim = info_pair.second;
        PAL::Request req(param.ioUnitInPage);
        uint64_t start_tick = tick;
        for (auto mapping: page_table) {
            if (mapping.second.first != victim_idx)
                continue;

            uint64_t lpn = mapping.first;
            uint32_t sw_block_idx = mapping.second.first;
            Block* rw_block;
            uint32_t lbn = lpn / param.pagesInBlock;
            uint32_t lpi = lpn % param.pagesInBlock;
            uint32_t old_pbn, new_pbn;

            {
                auto iter = blocks.find(sw_block_idx);
                if (iter == blocks.end())
                    panic("In RW block page allocation: RW block not found!\n");

                rw_block = &iter->second;
            }

            {
                auto iter = tool_src_table.find(lbn);
                if (iter == tool_src_table.end()) {
                    old_pbn = block_table[lbn];
                    auto iter = blocks.find(old_pbn);
                    tool_src_table[lbn] = make_pair(old_pbn, &iter->second);
                }
            }

            {
                auto iter = tool_dst_table.find(lbn);
                if (iter == tool_src_table.end()) {
                    new_pbn = allocateFreeBlock();
                    auto iter = blocks.find(new_pbn);
                    tool_dst_table[lbn] = make_pair(new_pbn, &iter->second);
                }
            }
            Block* new_data_block = tool_dst_table[lbn].second;

            ReadAndWrite(
                lbn,
                rw_block, sw_block_idx,
                new_data_block, new_pbn,
                lpi, tick
            );
            tool_mapping_update_list.push_back(lpn);
        }
        // Update RW page mapping table
        for (int i=0; i<(int)tool_mapping_update_list.size(); i++) {
            uint64_t lpn = tool_mapping_update_list[i];
            page_table.erase(lpn);
        }
        // Move valid pages from old data block to new data block
        for (auto mapping: tool_dst_table) {
            uint32_t lbn = mapping.first;
            Block* old_data_block = tool_src_table[lbn].second;
            uint32_t old_data_block_idx = tool_src_table[lbn].first;
            Block* new_data_block = mapping.second.second;
            uint32_t new_data_block_idx = mapping.second.first;

            for (uint32_t i=0; i<param.pagesInBlock; i++) {
                if (!old_data_block->checkValid(i, 0))
                    continue;

                ReadAndWrite(
                    lbn,
                    old_data_block,  old_data_block_idx,
                    new_data_block, new_data_block_idx,
                    i, tick
                );
            }

            block_table[lbn] = new_data_block_idx;
            blockErase(old_data_block_idx, tick);
            if (sw_info != nullptr && sw_info->belong_data_block_logical_idx == lbn)
                sw_info->belong_data_block_physical_idx = new_data_block_idx;
        }
        blockErase(victim_idx, tick);
        while (!tool_src_table.empty())
            tool_src_table.erase(tool_src_table.begin());
        while (!tool_dst_table.empty())
            tool_dst_table.erase(tool_dst_table.begin());
        while (!tool_mapping_update_list.empty())
            tool_mapping_update_list.erase(tool_mapping_update_list.begin());

        tick = start_tick + (tick - start_tick) / PARA_NUM;
//        debugprint(LOG_FTL_FAST, "GC ends");

        // Allocate a new rw block
        uint32_t new_rw_block_idx = allocateFreeBlock();
        Block* new_rw_block;
        std::unordered_map<uint32_t, Block>::iterator block_iter;
        block_iter = blocks.find(new_rw_block_idx);
        if (block_iter == blocks.end())
            panic("In RW block page allocation: block not found!\n");
        new_rw_block = &block_iter->second;

        // Update RW block list
        RWBlocks.erase(RWBlocks.begin());
        RWBlocks.push_back(make_pair(new_rw_block_idx, new_rw_block));

        clean_page_found = true;
        dst_pbn = new_rw_block_idx;
        dst_ppi = 0;
    }

    return make_pair(dst_pbn, dst_ppi);
}

void FAST::mergeSwitch(uint64_t& tick) {
//    debugprint(LOG_FTL_FAST, "Merge(switch) is triggered");

    if (sw_info == nullptr)
        panic("In merge (switch): SW block doesn't exist.");

    uint32_t data_block_physical_idx = sw_info->belong_data_block_physical_idx;
    uint32_t data_block_logical_idx = sw_info->belong_data_block_logical_idx;
    uint32_t sw_block_physical_idx = sw_info->block_idx;

    // Update block mapping table
    block_table[data_block_logical_idx] = sw_block_physical_idx;

    blockErase(data_block_physical_idx, tick);

    delete sw_info;
    sw_info = nullptr;

//    debugprint(LOG_FTL_FAST, "Merge(switch) ends");
}

void FAST::mergePartial(uint64_t& tick) {
//    debugprint(LOG_FTL_FAST, "Merge(partial) is triggered");

    if (sw_info == nullptr)
        panic("In merge (partial): SW block doesn't exist.");

    uint32_t data_block_physical_idx = sw_info->belong_data_block_physical_idx;
    uint32_t data_block_logical_idx = sw_info->belong_data_block_logical_idx;
    uint32_t sw_block_physical_idx = sw_info->block_idx;

    std::unordered_map<uint32_t, Block>::iterator data_block;
    data_block = blocks.find(data_block_physical_idx);
    std::unordered_map<uint32_t, Block>::iterator sw_block;
    sw_block = blocks.find(sw_block_physical_idx);
    PAL::Request req(param.ioUnitInPage);
    for (uint32_t i=sw_info->cur_page_idx; i<param.pagesInBlock; i++) {
        if (data_block->second.checkValid(i, 0)) {
            ReadAndWrite(
                data_block_logical_idx,
                &data_block->second, data_block->first,
                &sw_block->second, sw_block->first,
                i, tick
            );
        }
    }

    // Update block mapping table
    block_table[data_block_logical_idx] = sw_block_physical_idx;

    blockErase(data_block_physical_idx, tick);

    delete sw_info;
    sw_info = nullptr;

//    debugprint(LOG_FTL_FAST, "Merge(partial) ends");
}

void FAST::readInternal(Request &req, uint64_t &tick) {
    PAL::Request palRequest(req);
    std::unordered_map<uint32_t, Block>::iterator block;

    uint64_t lpn = req.lpn;
    uint32_t lbn, page_index;

    // Calculate lbn and index
    lbn = lpn / param.pagesInBlock;
    page_index = lpn % param.pagesInBlock;

    uint32_t pbn, ppi;
    ppi = page_index;
    auto iter = block_table.find(lbn);
    if (iter != block_table.end()) {
        if (bRandomTweak) {
            pDRAM->read(NULL, 8 * req.ioFlag.count(), tick);
        }
        else {
            pDRAM->read(NULL, 8, tick);
        }

        pbn = iter->second;

        // Try to read data block
        block = blocks.find(pbn);
        if (block == blocks.end())
            panic("In read request: data block not found!\n");

        pageRead(pbn, ppi, req, tick);

        // Try to read SW block
        if (sw_info != nullptr && sw_info->belong_data_block_physical_idx == pbn)
            pageRead(sw_info->block_idx, ppi, req, tick);

        // Try to read RW block
        auto iter = page_table.find(lpn);
        if (iter != page_table.end())
            pageRead(iter->second.first, iter->second.second, req, tick);
    }
}

/** Return true if invalid a rw block page successfully. */
bool FAST::invalidRWBlockPage(uint64_t lpn, uint32_t& src_pbn, uint32_t& src_ppi) {
    std::unordered_map<uint32_t, Block>::iterator rw_block;
    auto iter = page_table.find(lpn);
    bool rw_page_exist = iter != page_table.end();

    // Invalid corresponding page in RW block
    if (rw_page_exist) {
        src_pbn = iter->second.first;
        src_ppi = iter->second.second;

        rw_block = blocks.find(iter->second.first);
        if (rw_block == blocks.end())
            panic("Block not found!\n");

        rw_block->second.invalidate(iter->second.second, 0);

        // Update page mapping in table
        page_table.erase(iter);

        return true;
    }
    else return false;
}

void FAST::writeInternal(Request &req, uint64_t &tick, bool sendToPAL) {
    PAL::Request palRequest(req);
    std::unordered_map<uint32_t, Block>::iterator block;
    bool readBeforeWrite = false;
    if (!bRandomTweak && !req.ioFlag.all()) {
        // We have to read old data
        readBeforeWrite = true;
    }

    uint64_t lpn = req.lpn;
    uint32_t lbn, page_index;

    // Calculate lbn and index
    lbn = lpn / param.pagesInBlock;
    page_index = lpn % param.pagesInBlock;

    DEBUG_TEST("3");

    uint32_t pbn, ppi;
    ppi = page_index;
    auto iter = block_table.find(lbn);
    if (iter != block_table.end()) {
        pbn = iter->second;
    }
    else {
        pbn = allocateFreeBlock ();
        auto ret = block_table.emplace (lbn, pbn);

        if (!ret.second) {
            panic("Failed to insert new mapping");
        }
    }

    DEBUG_TEST_TICK(tick);

    if (sendToPAL) {
        if (bRandomTweak) {
            pDRAM->read(NULL, 8 * req.ioFlag.count(), tick);
            pDRAM->write(NULL, 8 * req.ioFlag.count(), tick);
        }
        else {
            pDRAM->read(NULL, 8, tick);
            pDRAM->write(NULL, 8, tick);
        }
    }

    DEBUG_TEST_TICK(tick);

    DEBUG_TEST("4");

    // Find the FTL block of index pbn
    block = blocks.find(pbn);

    uint32_t src_pbn, src_ppi;
    if (ppi == 0) {
        DEBUG_TEST("5");
        // The page is clean, write data to the page directly
        if (block->second.checkClean(ppi, 0)) {
            DEBUG_TEST("5-1");
            if (sw_info != nullptr && sw_info->belong_data_block_logical_idx == lbn)
                panic("Unknown error!\n");

            if (!invalidRWBlockPage(lpn, src_pbn, src_ppi))
                readBeforeWrite = false;

            pageWrite (src_pbn, src_ppi, pbn, ppi, req, readBeforeWrite, sendToPAL, tick);
        }
        // Allocate SW block
        else {
            DEBUG_TEST("5-2");
            if (block->second.checkValid(ppi, 0)) {
                // Invalidate current page
                block->second.invalidate(ppi, 0);

                if (readBeforeWrite && sendToPAL) {
                    palRequest.blockIndex = pbn;
                    palRequest.pageIndex = ppi;

                    palRequest.ioFlag = req.ioFlag;
                    palRequest.ioFlag.flip();

                    pPAL->read(palRequest, tick);
                }

                readBeforeWrite = false;
            }
            else if(!invalidRWBlockPage(lpn, src_pbn, src_ppi))
                readBeforeWrite = false;

            DEBUG_TEST("5-2 | 1");

            if (sw_info != nullptr) {
                if (sw_info->res_page_cnt == 0) {
                    DEBUG_TEST("5-2-1");
                    if (sw_info->belong_data_block_physical_idx == pbn)
                        pbn = sw_info->block_idx;
                    mergeSwitch(tick);
                }
                else {
                    DEBUG_TEST("5-2-2");
                    if (sw_info->belong_data_block_physical_idx == pbn)
                        pbn = sw_info->block_idx;
                    mergePartial(tick);
                }
            }

            DEBUG_TEST("5-2 | 2");

            uint32_t new_sw_block_idx = allocateFreeBlock();

            DEBUG_TEST("5-2 | 3");

            // Update SW block information
            initSWBlockInfo(new_sw_block_idx, pbn, lbn);

            DEBUG_TEST("5-2 | 4");

            pageWrite(src_pbn, src_ppi, sw_info->block_idx, ppi, req, readBeforeWrite, sendToPAL, tick);
            sw_info->cur_page_idx++;
            sw_info->res_page_cnt--;
            sw_info->last_access_ppi = ppi;
        }
    }
    else {
        DEBUG_TEST("6");
        if (sw_info != nullptr && sw_info->belong_data_block_logical_idx == lbn) {
            DEBUG_TEST("6-1");
            uint32_t last_ppi = sw_info->last_access_ppi;
            if (last_ppi + 1 == ppi) {
                if (block->second.checkValid(ppi, 0)) {
                    block->second.invalidate(ppi, 0);

                    src_pbn = pbn;
                    src_ppi = ppi;
                }
                else if(!invalidRWBlockPage(lpn, src_pbn, src_ppi))
                    readBeforeWrite = false;

                pageWrite(src_pbn, src_ppi, sw_info->block_idx, ppi, req, readBeforeWrite, sendToPAL, tick);
                sw_info->cur_page_idx++;
                sw_info->res_page_cnt--;
                sw_info->last_access_ppi = ppi;
            }
            else {
                // Here is different from the procedure in paper
                pbn = sw_info->block_idx;
                mergePartial(tick);

                // Notice that there is no data written here
            }
        }

        if (sw_info == nullptr || sw_info->belong_data_block_logical_idx != lbn) {
            DEBUG_TEST("6-2");
            std::unordered_map<uint32_t, Block>::iterator data_block;
            data_block = blocks.find(pbn);
            bool page_clean = data_block->second.checkClean(ppi, 0);
            bool page_valid = data_block->second.checkValid(ppi, 0);

            if (invalidRWBlockPage(lpn, src_pbn, src_ppi)) {
                if (readBeforeWrite && sendToPAL) {
                    palRequest.blockIndex = src_pbn;
                    palRequest.pageIndex = src_ppi;

                    palRequest.ioFlag = req.ioFlag;
                    palRequest.ioFlag.flip();

                    pPAL->read(palRequest, tick);
                }
            }

            // Write data
            if (page_clean) {
                // Write data to the data block
                pageWrite(0, 0, pbn, ppi, req, false, sendToPAL, tick);
            }
            else{
                if (page_valid) {
                    data_block->second.invalidate(ppi, 0);

                    if (readBeforeWrite && sendToPAL) {
                        palRequest.blockIndex = pbn;
                        palRequest.pageIndex = ppi;

                        palRequest.ioFlag = req.ioFlag;
                        palRequest.ioFlag.flip();

                        pPAL->read(palRequest, tick);
                    }
                }

                // Write data to RW blocks
                auto ret = allocateRWPage(tick);

                uint32_t dst_pbn, dst_ppi;
                dst_pbn = ret.first, dst_ppi = ret.second;

                pageWrite(0, 0, dst_pbn, dst_ppi, req, false, sendToPAL, tick);
                // Update page mapping in table
                page_table[lpn] = ret;
            }
        }
    }

    DEBUG_TEST_TICK(tick);

}

}

}
