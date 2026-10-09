// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Trash.h"

#include "TrashCommon.h"

#import <Foundation/Foundation.h>

namespace fs = std::filesystem;

namespace asma {

namespace {

NSURL* urlOf(const fs::path& path)
{
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}

} // namespace

bool trashAvailable(const fs::path& file)
{
    @autoreleasepool {
        NSError* error = nil;
        NSURL* url = urlOf(file);
        NSURL* trash = [[NSFileManager defaultManager] URLForDirectory:NSTrashDirectory
                                                             inDomain:NSUserDomainMask
                                                    appropriateForURL:url
                                                               create:NO
                                                                error:&error];
        if (trash != nil) return true;
        // A volume nothing was ever trashed from has no .Trashes folder yet:
        // trashItemAtURL makes one. Only a read-only volume surely has none;
        // anywhere else the move itself decides, and a refusal moves nothing.
        NSNumber* readOnly = nil;
        if (![url getResourceValue:&readOnly forKey:NSURLVolumeIsReadOnlyKey error:nil] || readOnly == nil) return false;
        return ![readOnly boolValue];
    }
}

TrashResult moveToTrash(const fs::path& file)
{
    @autoreleasepool {
        TrashResult result;
        NSURL* where = nil;
        NSError* error = nil;
        if ([[NSFileManager defaultManager] trashItemAtURL:urlOf(file) resultingItemURL:&where error:&error] && where) {
            result.ok = true;
            result.where = fs::path([[where path] fileSystemRepresentation]);
        } else {
            result.error = error ? [[error localizedDescription] UTF8String] : "cannot move it to the Trash";
        }
        return result;
    }
}

std::string restoreFromTrash(const fs::path& where, const fs::path& to) { return detail::renameBack(where, to); }

} // namespace asma
