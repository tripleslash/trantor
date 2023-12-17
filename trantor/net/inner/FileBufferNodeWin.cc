#include <trantor/net/inner/BufferNode.h>
#include <windows.h>

namespace trantor
{
static const size_t kMaxSendFileBufferSize = 16 * 1024;
class FileBufferNode : public BufferNode
{
  public:
    FileBufferNode(const wchar_t *fileName, long long offset, size_t length)
    {
        sendHandle_ = CreateFileW(fileName,
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (sendHandle_ == INVALID_HANDLE_VALUE)
        {
            LOG_SYSERR << fileName << " open error";
            isDone_ = true;
            return;
        }
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(sendHandle_, &fileSize))
        {
            LOG_SYSERR << fileName << " stat error";
            CloseHandle(sendHandle_);
            sendHandle_ = INVALID_HANDLE_VALUE;
            isDone_ = true;
            return;
        }

        if (length == 0)
        {
            if (offset >= fileSize.QuadPart)
            {
                LOG_ERROR << "The file size is " << fileSize.QuadPart
                          << " bytes, but the offset is " << offset
                          << " bytes and the length is " << length << " bytes";
                CloseHandle(sendHandle_);
                sendHandle_ = INVALID_HANDLE_VALUE;
                isDone_ = true;
                return;
            }
            fileBytesToSend_ = fileSize.QuadPart - offset;
        }
        else
        {
            if (length + offset > fileSize.QuadPart)
            {
                LOG_ERROR << "The file size is " << fileSize.QuadPart
                          << " bytes, but the offset is " << offset
                          << " bytes and the length is " << length << " bytes";
                CloseHandle(sendHandle_);
                sendHandle_ = INVALID_HANDLE_VALUE;
                isDone_ = true;
                return;
            }

            fileBytesToSend_ = length;
        }
        LARGE_INTEGER li;
        li.QuadPart = offset;
        if (!SetFilePointerEx(sendHandle_, li, nullptr, FILE_BEGIN))
        {
            LOG_SYSERR << fileName << " seek error";
            CloseHandle(sendHandle_);
            sendHandle_ = INVALID_HANDLE_VALUE;
            isDone_ = true;
            return;
        }
    }

    bool isFile() const override
    {
        return true;
    }

    void getData(const char *&data, size_t &len) override
    {
        if (msgBuffer_.readableBytes() == 0 && fileBytesToSend_ > 0 &&
            sendHandle_ != INVALID_HANDLE_VALUE)
        {
            msgBuffer_.ensureWritableBytes(kMaxSendFileBufferSize <
                                                   fileBytesToSend_
                                               ? kMaxSendFileBufferSize
                                               : fileBytesToSend_);
            DWORD n = 0;
            if (!ReadFile(sendHandle_,
                          msgBuffer_.beginWrite(),
                          msgBuffer_.writableBytes(),
                          &n,
                          nullptr))
            {
                LOG_SYSERR << "FileBufferNode::getData()";
            }
            if (n > 0)
            {
                msgBuffer_.hasWritten(n);
            }
            else if (n == 0)
            {
                LOG_TRACE << "Read the end of file.";
            }
            else
            {
                LOG_SYSERR << "FileBufferNode::getData()";
            }
        }
        data = msgBuffer_.peek();
        len = msgBuffer_.readableBytes();
    }
    void retrieve(size_t len) override
    {
        msgBuffer_.retrieve(len);
        fileBytesToSend_ -= len;
    }
    size_t remainingBytes() const override
    {
        if (isDone_)
            return 0;
        return fileBytesToSend_;
    }
    ~FileBufferNode() override
    {
        if (sendHandle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(sendHandle_);
        }
    }
    int getFd() const override
    {
        LOG_ERROR << "getFd() is not supported on Windows";
        return 0;
    }
    bool available() const override
    {
        return sendHandle_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE sendHandle_{INVALID_HANDLE_VALUE};
    size_t fileBytesToSend_{0};
    MsgBuffer msgBuffer_;
};
BufferNodePtr BufferNode::newFileBufferNode(const wchar_t *fileName,
                                            long long offset,
                                            size_t length)
{
    return std::make_shared<FileBufferNode>(fileName, offset, length);
}
}  // namespace trantor