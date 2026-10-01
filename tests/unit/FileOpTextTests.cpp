// File-operation status and dialog text (T072; UI contract §6; spec US4-6).

#include <te/app/FileOpText.h>

#include <gtest/gtest.h>

namespace
{

using te::FileOpFinalState;
using te::FileOpKind;

TEST(FileOpText, ProgressNamesTheOperationAndCount)
{
    const te::FileOpText text;
    EXPECT_EQ(text.Progress(FileOpKind::Copy, 3), L"Copying 3 items…");
    EXPECT_EQ(text.Progress(FileOpKind::Move, 12), L"Moving 12 items…");
    EXPECT_EQ(text.Progress(FileOpKind::Recycle, 1), L"Deleting 1 items…");
    EXPECT_EQ(text.Progress(FileOpKind::DeletePermanent, 2), L"Deleting 2 items…");
    EXPECT_EQ(text.Progress(FileOpKind::Rename, 1), L"Renaming…");
}

TEST(FileOpText, ResultsFollowTheUiContract)
{
    const te::FileOpText text;
    EXPECT_EQ(text.Result(FileOpKind::Copy, FileOpFinalState::Succeeded, 3, 3), L"3 items copied");
    EXPECT_EQ(text.Result(FileOpKind::Move, FileOpFinalState::Succeeded, 2, 2), L"2 items moved");
    EXPECT_EQ(text.Result(FileOpKind::Recycle, FileOpFinalState::Succeeded, 4, 4), L"4 items deleted");
    EXPECT_EQ(text.Result(FileOpKind::Rename, FileOpFinalState::Succeeded, 1, 1), L"Item renamed");

    EXPECT_EQ(text.Result(FileOpKind::Copy, FileOpFinalState::PartiallySucceeded, 2, 5),
              L"2 of 5 items completed");
    EXPECT_EQ(text.Result(FileOpKind::Copy, FileOpFinalState::Failed, 0, 1), L"0 of 1 items completed");
    EXPECT_EQ(text.Result(FileOpKind::Move, FileOpFinalState::Cancelled, 4, 10),
              L"Operation cancelled — 4 items completed before cancellation");
}

TEST(FileOpText, SkippedItemsAreNotReportedAsDone)
{
    // The user chose Skip for two conflicts: the operation succeeded, but not every item
    // was copied, and the status must not claim otherwise.
    const te::FileOpText text;
    EXPECT_EQ(text.Result(FileOpKind::Copy, FileOpFinalState::Succeeded, 3, 5), L"3 of 5 items completed");
}

TEST(FileOpText, FailureListHasOneLinePerItemAndIsBounded)
{
    std::vector<te::Status> errors;
    for (int i = 0; i < 5; ++i)
    {
        errors.push_back({E_ACCESSDENIED, L"file" + std::to_wstring(i) + L".txt: Access is denied."});
    }
    EXPECT_EQ(te::FileOpText::FailureList(errors, 10),
              L"file0.txt: Access is denied.\nfile1.txt: Access is denied.\nfile2.txt: Access is denied.\n"
              L"file3.txt: Access is denied.\nfile4.txt: Access is denied.");
    EXPECT_EQ(te::FileOpText::FailureList(errors, 2),
              L"file0.txt: Access is denied.\nfile1.txt: Access is denied.\n…");
    EXPECT_EQ(te::FileOpText::FailureList({}, 10), L"");
}

TEST(FileOpText, LoadKeepsDefaultsForMissingIds)
{
    const te::FileOpText text = te::FileOpText::Load(GetModuleHandleW(nullptr), {});
    EXPECT_EQ(text.copiedFmt, te::FileOpText{}.copiedFmt);
}

} // namespace
