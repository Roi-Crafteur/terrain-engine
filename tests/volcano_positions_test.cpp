// Where the volcanoes stand (agent 45, generation/volcano_placement.hpp): the constants of the core,
// never the data. 100 positions of the default world (one volcano in 37, bit for bit, with their
// origin and whether active and large), pinned with the code of the settlement before the draw
// moved into generation/; the 3,759 volcanoes the settlement reads (buildGeology) are the same
// list, with data/settlement/world.json and with a copy whose area of the reference planet and
// arcane craters changed; the same bits on 1 and 4 threads.
#include "geography_plates.hpp"
#include "volcano_placement.hpp"

#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

namespace st = terrain::settlement;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr std::size_t kVolcanoes = 3759;

struct Pinned {
    std::size_t index;
    std::uint64_t x; // the bits of the double
    std::uint64_t z;
    int origin;
    int active;
    int large;
};

// Measured with the placement of the settlement (settlement/geology_points.cpp) before agent 45
// moved it into generation/volcano_placement.cpp.
constexpr Pinned kPinned[] = {
    {0, 0x416BBC64ED846E0CULL, 0xC166196688B3A30EULL, 0, 0, 0},
    {37, 0xC17480E5E01F5224ULL, 0x413A2AF9CFE18DB8ULL, 0, 0, 0},
    {74, 0x414D3D448BF56174ULL, 0x4167F17264050C0AULL, 0, 0, 0},
    {111, 0x417B8E8A6F33033AULL, 0x41703F0FB88568A4ULL, 0, 0, 0},
    {148, 0x4168974C219B4D33ULL, 0xC16BE01ED0E9DE8BULL, 0, 0, 0},
    {185, 0xC16927FF80F661A8ULL, 0xC15E4706F7530678ULL, 0, 0, 0},
    {222, 0x4179B8A6399FD246ULL, 0xC16C7662BD9B4223ULL, 0, 0, 0},
    {259, 0x415724741C0A9AFEULL, 0x40E0871B07302036ULL, 0, 0, 0},
    {296, 0x417B52FD07C4F87EULL, 0x4170935A57A54AC9ULL, 0, 0, 0},
    {333, 0xC1745931F06D0B7CULL, 0xC16D30C0D0F980E4ULL, 0, 0, 0},
    {370, 0x414A16A8DD97E760ULL, 0xC12D7EC1A14E6105ULL, 0, 0, 0},
    {407, 0xC16919BE60397258ULL, 0x4131B71933C1753DULL, 0, 0, 0},
    {444, 0x414520D7F1DE0CB0ULL, 0xC1644A733A92F6FBULL, 0, 0, 0},
    {481, 0x4179216440D54D9CULL, 0xC15510A98616ED89ULL, 0, 0, 0},
    {518, 0xC1744DC4CE16265CULL, 0xC16C011C6B789A61ULL, 0, 1, 0},
    {555, 0xC16B766CE9589BD6ULL, 0x414AFFFFAB864170ULL, 0, 0, 0},
    {592, 0x417AD4E9C4030476ULL, 0xC16FD84AD95962CCULL, 0, 1, 0},
    {629, 0x4156287B6C101DCEULL, 0xC15E9F3E858ECFCAULL, 0, 1, 0},
    {666, 0xC1733044E5D37F40ULL, 0xC132AAC353DBB6BEULL, 0, 0, 0},
    {703, 0x417823BA73F5D3EFULL, 0xC1482CE037CA13E9ULL, 0, 0, 0},
    {740, 0x41676C26FED3C325ULL, 0xC16D3F51B1576215ULL, 0, 0, 0},
    {777, 0x41716914FB080645ULL, 0x410897CDF994693EULL, 0, 0, 0},
    {814, 0x41747D36E4EC58E4ULL, 0xC16106AC82842804ULL, 0, 0, 0},
    {851, 0xC16B309306EBC5B0ULL, 0x414A8A2C55587FC3ULL, 0, 0, 0},
    {888, 0x4144C094B44614C4ULL, 0xC1644273687AF00CULL, 0, 0, 0},
    {925, 0xC17BEFB87BF96341ULL, 0x416BD11B6D939123ULL, 0, 1, 0},
    {962, 0xC16C939E436768BEULL, 0x414F2F6F4B7D95C5ULL, 0, 0, 0},
    {999, 0xC176E61B00759188ULL, 0xC15903608EE72D42ULL, 0, 0, 0},
    {1036, 0x415F11B0B9DA4135ULL, 0x4156FDE0C4659AA7ULL, 0, 0, 0},
    {1073, 0x4154D2817E043CE0ULL, 0xC15FD7905D98E0F3ULL, 0, 0, 0},
    {1110, 0x417A472E645AA45DULL, 0x416997ABAD6CD1B2ULL, 0, 0, 0},
    {1147, 0xC16DBB6C764442A2ULL, 0x4152E67F405D4237ULL, 0, 0, 0},
    {1184, 0x4172A9A9CB4DF421ULL, 0xC1697A0D83A5E6DCULL, 0, 0, 0},
    {1221, 0x41793566B8AF7E26ULL, 0xC1565C4F7269A7DDULL, 0, 0, 0},
    {1258, 0x4162E5691451CEDBULL, 0x41520ED89A056F31ULL, 0, 0, 0},
    {1295, 0x4161287DD81314D0ULL, 0xC16C220B8B9526B7ULL, 0, 0, 0},
    {1332, 0xC17344D2C42BAFC0ULL, 0xC141D301F4AF1A01ULL, 0, 0, 0},
    {1369, 0x416CE5151720131FULL, 0x4162A654D74AD12FULL, 0, 1, 0},
    {1406, 0xC16A5DC7D99574C8ULL, 0xC15BB8607A351338ULL, 0, 0, 0},
    {1443, 0xC1683710F6830B72ULL, 0x41407AF3ACF801A4ULL, 0, 0, 0},
    {1480, 0xC1744B7412B64A58ULL, 0xC16B76026D3C111DULL, 0, 0, 0},
    {1517, 0x41740CE7A2C55E7DULL, 0xC162CE068F083618ULL, 0, 0, 0},
    {1554, 0xC16E7C3B1B5E3640ULL, 0x41519C9BD4A12185ULL, 0, 0, 0},
    {1591, 0xC178F8DCE9CF15B0ULL, 0x41425557032901E5ULL, 0, 0, 0},
    {1628, 0x416C263E986C8DC0ULL, 0x416EDAB82D409F56ULL, 0, 1, 0},
    {1665, 0xC17B527B97791094ULL, 0x415239395EDBFC77ULL, 0, 1, 0},
    {1702, 0x4167C89BF048C4A0ULL, 0x4152DAB1036ABCE0ULL, 0, 0, 0},
    {1739, 0x416C5EDCB4E91220ULL, 0xC165688BD7A7855CULL, 0, 1, 0},
    {1776, 0x41350910B6317AE8ULL, 0xC167993F2DF1E2A4ULL, 0, 0, 0},
    {1813, 0x417C55A07A2E8E1CULL, 0x416B5084174DD63FULL, 0, 0, 0},
    {1850, 0x4169841146D5A05EULL, 0xC16B1808FAD79C01ULL, 0, 0, 0},
    {1887, 0x4173D44BBE4DD8E6ULL, 0xC16523275214253BULL, 0, 1, 0},
    {1924, 0xC17321B98C746478ULL, 0xC13EC4A1637799D5ULL, 0, 0, 0},
    {1961, 0xC1619F3DC7DBCAB2ULL, 0x4140F7AC2CA9A313ULL, 0, 0, 0},
    {1998, 0x417BC8AEF83F7BDCULL, 0x417017B789B1B2ACULL, 0, 0, 0},
    {2035, 0x413463A12C5FFA40ULL, 0xC167B14C46D97966ULL, 0, 0, 0},
    {2072, 0x4152FFE10DF2787CULL, 0xC16033EC486C9B42ULL, 0, 0, 0},
    {2109, 0xC15CD7EEE7B92540ULL, 0xC15F2D89ACF059D4ULL, 0, 1, 0},
    {2146, 0x41799772723E7E24ULL, 0xC16CC1019D871CB9ULL, 0, 0, 0},
    {2183, 0x417A123250D743AAULL, 0x41695FB56952D4C7ULL, 0, 0, 0},
    {2220, 0xC17A6A3CF35188BAULL, 0x4166ABA5A67D3ED4ULL, 0, 0, 0},
    {2257, 0x416C011D978937C6ULL, 0x41620DC941EAD36DULL, 0, 0, 0},
    {2294, 0x414B3BA3B5174054ULL, 0xC13BC9035C0D7F2EULL, 0, 1, 0},
    {2331, 0x4137B2021A54F720ULL, 0x415362A4E4E53E1CULL, 0, 0, 0},
    {2368, 0x414046922442E34CULL, 0xC16B89DF4F2D8015ULL, 0, 0, 0},
    {2405, 0xC16CA165D0DA733CULL, 0xC157A8047066343BULL, 0, 0, 0},
    {2442, 0xC17554B22F7B9B86ULL, 0xC1705DDC381F0A25ULL, 0, 0, 0},
    {2479, 0x4172428F3992E46CULL, 0xC14336E2EA2C2AABULL, 0, 0, 0},
    {2516, 0x413EC9ABE782C1F8ULL, 0xC16AC91FCEAA958DULL, 0, 1, 0},
    {2553, 0x4163F14F7E8D141DULL, 0x41690DF30AE5CAF6ULL, 0, 0, 0},
    {2590, 0x416C1D24CA2D5E2FULL, 0xC164FE93CD42E88DULL, 0, 0, 0},
    {2627, 0x41746AFBF013CCFCULL, 0x4164060E8F3A4A40ULL, 0, 0, 0},
    {2664, 0x41784A339D7D8C1CULL, 0xC16D04AF28740A17ULL, 0, 0, 0},
    {2701, 0xC15D746131F94C4CULL, 0xC15E97C093396A05ULL, 0, 0, 0},
    {2738, 0x4174C47D2F3A140AULL, 0x416EEDF73F0217ACULL, 0, 0, 0},
    {2775, 0xC173448DEAF33544ULL, 0xC13601DD5A78FD4EULL, 0, 0, 0},
    {2812, 0x416CCB54588F31B8ULL, 0x4162DEF1DC23E44CULL, 0, 0, 0},
    {2849, 0x417C2F43FAAEEB3AULL, 0x416EFFA3CE2497E7ULL, 0, 0, 0},
    {2886, 0x41674D788256844BULL, 0xC16F4DBA70FE49A7ULL, 0, 0, 0},
    {2923, 0xC170D33E1EED3670ULL, 0xC15A0DC9D9A93E37ULL, 0, 0, 0},
    {2960, 0xC17327CBE2E9A6B4ULL, 0xC13E918427D130D9ULL, 0, 0, 0},
    {2997, 0x4123C9EC342CA0B0ULL, 0x4153729DD263527BULL, 0, 0, 1},
    {3034, 0xC15DDD9969D5FE50ULL, 0xC16F76CF5C6326FDULL, 1, 0, 0},
    {3071, 0x4172ED0BE23C4302ULL, 0xC1332104B38A08A3ULL, 1, 0, 0},
    {3108, 0x4179BFE08C73B738ULL, 0x4161D26E2C3A3ABFULL, 1, 0, 0},
    {3145, 0x416C45D1AEAE7639ULL, 0x414635491BC87B05ULL, 1, 0, 0},
    {3182, 0x415E40CEF6E7290EULL, 0xC1145BDACEC8D472ULL, 1, 0, 0},
    {3219, 0xC16111DA4799E282ULL, 0xC16E7ED933E536F6ULL, 1, 0, 0},
    {3256, 0x416BBB6F984546D7ULL, 0xC15DBA8783803655ULL, 1, 0, 0},
    {3293, 0xC16004F9936F9D44ULL, 0x4155AF9C221358E5ULL, 1, 0, 0},
    {3330, 0xC158D40FAE29CD40ULL, 0x4158A08DCCE4E4F0ULL, 1, 0, 0},
    {3367, 0x41570AAEFA755727ULL, 0x4158BBC1B5B032E0ULL, 2, 0, 0},
    {3404, 0xC15B37069C041DE0ULL, 0x40FFA8D9C13289EEULL, 2, 0, 0},
    {3441, 0x414C501990E61DA0ULL, 0x41585082A7831903ULL, 2, 0, 0},
    {3478, 0x412B16E71A54D0A0ULL, 0xC155115B417AC175ULL, 2, 0, 0},
    {3515, 0x411150EAA6247D60ULL, 0xC1553E0FF6BC4BDFULL, 2, 1, 0},
    {3552, 0x416D7B8EFB5445F6ULL, 0xC1457C78F09DF957ULL, 2, 0, 0},
    {3589, 0xC176481451C30EC3ULL, 0x416EC42B8B89C368ULL, 3, 0, 0},
    {3626, 0x4136F0099042E647ULL, 0x41543ED9FBD4BC76ULL, 3, 0, 0},
    {3663, 0xC175955728B75B54ULL, 0xC158E7C13602F1D8ULL, 3, 0, 0},
};

std::vector<terrain::PlacedVolcano> placed(const terrain::Geography& geo, int threads) {
    const terrain::GeographyPlates plates(kSeed, geo, threads);
    const terrain::VolcanoParams params;
    return terrain::placeVolcanoes(plates, kSeed, params,
                                   terrain::volcanoScale(geo.world(), params), threads);
}

void testPinned(const std::vector<terrain::PlacedVolcano>& v) {
    int differ = 0;
    for (const Pinned& p : kPinned) {
        const terrain::PlacedVolcano& q = v[p.index];
        const bool same = std::bit_cast<std::uint64_t>(q.x) == p.x &&
                          std::bit_cast<std::uint64_t>(q.z) == p.z &&
                          static_cast<int>(q.origin) == p.origin && (q.active ? 1 : 0) == p.active &&
                          (q.large ? 1 : 0) == p.large;
        differ += same ? 0 : 1;
    }
    std::printf("%zu volcanoes; of the %zu pinned, %d differ\n", v.size(), std::size(kPinned),
                differ);
    TEST_CHECK(v.size() == kVolcanoes && std::size(kPinned) == 100 && differ == 0);
}

// The geology of the settlement on a coarse grid (the volcanoes never depend on the grid).
st::Geology geologyOf(const terrain::Geography& geo, const st::SettlementConfig& config) {
    const terrain::PlanetGrid grid = terrain::makePlanetGrid(geo.world(), 65536.0);
    std::vector<std::uint32_t> cells;
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            if (geo.sampleAt(grid.centerX(i), grid.centerZ(j)).coast > 0.0) {
                cells.push_back(static_cast<std::uint32_t>(grid.index(i, j)));
            }
        }
    }
    const auto land = std::make_shared<const terrain::planet::LandIndex>(
        terrain::planet::LandIndex::ofCells(grid.cells(), cells));
    return st::buildGeology(grid, land, geo, kSeed, config, 0);
}

bool sameList(const std::vector<terrain::PlacedVolcano>& a, const std::vector<st::Volcano>& b) {
    bool same = a.size() == b.size();
    for (std::size_t k = 0; same && k < a.size(); ++k) {
        same = a[k].x == b[k].x && a[k].z == b[k].z &&
               static_cast<int>(a[k].origin) == static_cast<int>(b[k].kind) &&
               a[k].active == b[k].active && a[k].large == b[k].large;
    }
    return same;
}

void testSettlement(const terrain::Geography& geo, const std::vector<terrain::PlacedVolcano>& v) {
    st::SettlementConfig config = st::loadSettlementData(TERRAIN_SETTLEMENT_DATA).config;
    const bool data = sameList(v, geologyOf(geo, config).volcanoes);
    config.referenceAreaKm2 *= 2.0;
    config.geology.arcaneCraters = {10, 20};
    config.geology.volcanoInfluenceKm = 90.0;
    const bool other = sameList(v, geologyOf(geo, config).volcanoes);
    std::printf("the volcanoes of the settlement: %s with world.json, %s with other data\n",
                data ? "the same" : "DIFFERENT", other ? "the same" : "DIFFERENT");
    TEST_CHECK(data && other);
}

} // namespace

int main() {
    const terrain::Heightfield field(kSeed);
    const terrain::Geography& geo = *field.geography();
    const std::vector<terrain::PlacedVolcano> one = placed(geo, 1);
    testPinned(one);
    const std::vector<terrain::PlacedVolcano> four = placed(geo, 4);
    bool same = one.size() == four.size();
    for (std::size_t k = 0; same && k < one.size(); ++k) {
        same = one[k].x == four[k].x && one[k].z == four[k].z && one[k].active == four[k].active;
    }
    TEST_CHECK(same);
    testSettlement(geo, one);
    return test::result();
}
