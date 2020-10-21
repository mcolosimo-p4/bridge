/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2020 Paradigm4 Inc.
* All Rights Reserved.
*
* s3bridge is a plugin for SciDB, an Open Source Array DBMS maintained
* by Paradigm4. See http://www.paradigm4.com/
*
* s3bridge is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* s3bridge is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with s3bridge.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

#ifndef S3_INDEX_H_
#define S3_INDEX_H_

#include <array/Coordinate.h>
#include <array/Dimensions.h>
#include <query/InstanceID.h>

#include <aws/core/utils/memory/stl/AWSStreamFwd.h>


// Forward Declarastions to avoid including full headers - speed-up
// compilation
namespace scidb {
    class ArrayDesc;
    class Query;
    class SharedBuffer;
}
// -- End of Forward Declarations


namespace scidb {
    class S3Index;
}
namespace std {
    std::ostream& operator<<(std::ostream&, const scidb::Coordinates&);
    std::ostream& operator<<(std::ostream&, const scidb::S3Index&);

    // Serialize & De-serialize to S3
    Aws::IOStream& operator<<(Aws::IOStream&, const scidb::Coordinates&);
    Aws::IOStream& operator<<(Aws::IOStream&, const scidb::S3Index&);
    Aws::IOStream& operator>>(Aws::IOStream&, scidb::S3Index&);
}

namespace scidb {
// --
// -- - S3Index - --
// --

// Type of S3Index Container
typedef std::vector<Coordinates> S3IndexCont;
// typedef std::set<Coordinates, CoordinatesLess> S3IndexCont;

class S3Index {
    friend Aws::IOStream& std::operator>>(Aws::IOStream&, scidb::S3Index&);
    friend Aws::IOStream& std::operator<<(Aws::IOStream&, const scidb::S3Index&);

  public:
    S3Index(const ArrayDesc&);

    size_t size() const;
    void insert(const Coordinates&);
    void sort();

    // Serialize & De-serialize for inter-instance comms
    std::shared_ptr<SharedBuffer> serialize() const;
    std::shared_ptr<SharedBuffer> filter_serialize(
        const size_t nInst, const InstanceID instID) const;
    void deserialize_insert(std::shared_ptr<SharedBuffer>);

    const S3IndexCont::const_iterator begin() const;
    const S3IndexCont::const_iterator end() const;

    const S3IndexCont::const_iterator find(const Coordinates&) const;
    void filter_trim(const size_t nInst, const InstanceID instID);

  private:
    const ArrayDesc& _desc;
    const Dimensions& _dims;
    const size_t _nDims;

    S3IndexCont _values;
};
}

#endif
