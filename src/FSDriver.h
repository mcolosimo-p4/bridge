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

#ifndef FS_DRIVER_H_
#define FS_DRIVER_H_

#include "Driver.h"


namespace scidb
{

class FSDriver: public Driver {
public:
    FSDriver(const std::string &url);

    void writeArrow(const std::string&,
                    std::shared_ptr<const arrow::Buffer>) const;

    void readMetadata(std::map<std::string, std::string>&) const;
    void writeMetadata(const std::map<std::string, std::string>&) const;

    // Count number of objects with specified prefix
    size_t count(const std::string&) const;

    // Return print-friendly path used by driver
    const std::string& getURL() const;

private:
    const std::string _url;
    std::string _prefix;

    size_t _readArrow(const std::string&, std::shared_ptr<arrow::Buffer>&, bool) const;
};

}

#endif