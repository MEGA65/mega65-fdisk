#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include <stdarg.h>
#include <stdio.h>

extern int real_main(int argc, char **argv);
extern int format_disk(void);
extern void open_sdcard_and_retrieve_details(void);
extern int are_there_gaps_between_files(void);

class M65FdiskTestFixture : public ::testing::Test {
  protected:
    void SetUp() override
    {
      // suppress the chatty output
      ::testing::internal::CaptureStderr();
      ::testing::internal::CaptureStdout();

      // generate empty 4gb sdcard.img file
      generate_empty_256mb_sdcard_img();

      setenv("SDCARDFILE", "sdcard.img", 1);
      setenv("FLASHFILE", "gtest/bin/mega65r3.cor", 1);
    }

    void TearDown() override
    {
      testing::internal::GetCapturedStderr();
      testing::internal::GetCapturedStdout();

      // cleanup to remove the dummy .bit and .cor files
      // remove("sdcard.img");
    }

    void generate_empty_256mb_sdcard_img(void)
    {
      FILE* fl = fopen("sdcard.img", "wb");
      for (int k = 0; k < 1*1024*1024*1024/16; k++) {
        // fputc(0, fl);
        fwrite("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",  16, 1, fl);
        //fwrite("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",  16, 1, fl);
        //fwrite("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",  16, 1, fl);
        //fwrite("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",  16, 1, fl);
        //fwrite("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",  16, 1, fl);
      }
      fclose(fl);
    }
};

TEST_F(M65FdiskTestFixture, AssureNoGapsBetweenPrePopulatedFiles)
{
  open_sdcard_and_retrieve_details();
  format_disk();
  ASSERT_EQ(0, are_there_gaps_between_files());
}
