#include "test.h"
#include "../src/platform/overlay.h"
#include "../src/import/ovl_probe.h"

void test_overlay(void)
{
    CHECK_EQ(ovl_current(), 0);

    CHECK_EQ(ovl_require(OVL_FILEDLG), ERR_OK);
    CHECK_EQ(ovl_current(), OVL_FILEDLG);
    CHECK_EQ(ovl1_probe(), OVL_PROBE_MAGIC + 1);

    CHECK_EQ(ovl_require(OVL_XLSX), ERR_OK);
    CHECK_EQ(ovl_current(), OVL_XLSX);
    CHECK_EQ(ovl3_probe(), OVL_PROBE_MAGIC + 3);

    ovl_invalidate();
    CHECK_EQ(ovl_current(), 0);

    CHECK_EQ(ovl_require(OVL_ZIP), ERR_OK);
    CHECK_EQ(ovl4_probe(), OVL_PROBE_MAGIC + 4);
    CHECK_EQ(ovl_require(OVL_XLSX), ERR_OK);
    CHECK_EQ(ovl5_probe(), OVL_PROBE_MAGIC + 5);
    CHECK_EQ(ovl_require(OVL_XLSX), ERR_OK);
    CHECK_EQ(ovl6_probe(), OVL_PROBE_MAGIC + 6);
    CHECK_EQ(ovl_require(OVL_XLSX), ERR_OK);
    CHECK_EQ(ovl7_probe(), OVL_PROBE_MAGIC + 7);

    CHECK_EQ(ovl_require(0), ERR_NOTFOUND);
    CHECK_EQ(ovl_require(99), ERR_NOTFOUND);
}
