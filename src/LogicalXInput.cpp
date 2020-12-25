/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2020 Paradigm4 Inc.
* All Rights Reserved.
*
* bridge is a plugin for SciDB, an Open Source Array DBMS maintained
* by Paradigm4. See http://www.paradigm4.com/
*
* bridge is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* bridge is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with bridge.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

#include "XInputSettings.h"
#include "Driver.h"

// SciDB
#include <query/LogicalQueryPlan.h>
#include <query/Parser.h>
#include <util/OnScopeExit.h>


namespace scidb {

class LogicalXInput : public  LogicalOperator
{
public:
    LogicalXInput(const std::string& logicalName, const std::string& alias):
        LogicalOperator(logicalName, alias)
    {}

    static PlistSpec const* makePlistSpec()
    {
        static PlistSpec argSpec {
            { "", // positionals
              RE(RE::STAR, {
                      RE(PP(PLACEHOLDER_CONSTANT, TID_STRING))
                  })
            },
            { KW_FORMAT,        RE(PP(PLACEHOLDER_CONSTANT, TID_STRING)) },
            { KW_CACHE_SIZE,    RE(PP(PLACEHOLDER_CONSTANT, TID_INT64))  }
        };
        return &argSpec;
    }

    ArrayDesc inferSchema(std::vector<ArrayDesc> schemas, std::shared_ptr<Query> query)
    {
        XInputSettings settings(_parameters, _kwParameters, true, query);

        // Get Metadata
        auto driver = Driver::makeDriver(settings.getURL());
        std::map<std::string, std::string> metadata;
        driver->readMetadata(metadata);
        LOG4CXX_DEBUG(logger, "XINPUT|" << query->getInstanceID() << "|schema: " << metadata["schema"]);

        // Build Fake Query and Extract Schema
        std::shared_ptr<Query> innerQuery = Query::createFakeQuery(
                         query->getPhysicalCoordinatorID(),
                         query->mapLogicalToPhysical(query->getInstanceID()),
                         query->getCoordinatorLiveness());

        // Create a scope where the query's arena is responsible for
        // memory allocation.
        {
            arena::ScopedArenaTLS arenaTLS(innerQuery->getArena());

            OnScopeExit fqd([&innerQuery] () {
                                Query::destroyFakeQuery(innerQuery.get()); });

            std::ostringstream out;
            auto schemaPair = metadata.find("schema");
            if (schemaPair == metadata.end())
                throw USER_EXCEPTION(scidb::SCIDB_SE_METADATA,
                                     scidb::SCIDB_LE_UNKNOWN_ERROR)
                    << "schema missing from metadata";
            out << "input(" << schemaPair->second << ", '/dev/null')";
            innerQuery->queryString = out.str();
            innerQuery->logicalPlan = std::make_shared<LogicalPlan>(
                parseStatement(innerQuery, true));
        }

        // Extract Schema and Set Distribution
        ArrayDesc schema = innerQuery->logicalPlan->inferTypes(innerQuery);
        schema.setDistribution(createDistribution(defaultDistType()));
        return schema;
    }
};

REGISTER_LOGICAL_OPERATOR_FACTORY(LogicalXInput, "xinput");

} // namespace scidb