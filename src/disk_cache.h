#ifndef __DISKCACHE_H__
#define __DISKCACHE_H__

#include "fasttun_base.h"

NAMESPACE_BEG(tun)

class DiskCache
{
  public:
    DiskCache()
            :mpFile(NULL)
    {}

    virtual ~DiskCache();

    ssize_t write(const void *data, size_t datalen);
    ssize_t read(void *data, size_t datalen);
    size_t peeksize();

    void rollback(size_t n);

    void clear();

  private:
    bool _createFile();

  private:
    FILE *mpFile;
};

NAMESPACE_END // namespace tun

#endif // __DISKCACHE_H__
