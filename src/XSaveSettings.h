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

#ifndef X_SAVE_SETTINGS
#define X_SAVE_SETTINGS

#include "Common.h"

// SciDB
#include <query/Expression.h>
#include <query/LogicalOperator.h>
#include <query/Query.h>


namespace scidb {
// Logger for operator. static to prevent visibility of variable outside of file
static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.operators.xsave"));

static const char* const KW_FORMAT	  = "format";
static const char* const KW_COMPRESSION	  = "compression";
static const char* const KW_INDEX_SPLIT	  = "index_split";

typedef std::shared_ptr<OperatorParamLogicalExpression> ParamType_t;

class XSaveSettings
{
public:
    static size_t chunkDataOffset()
    {
        return (sizeof(ConstRLEPayload::Header) + 2 * sizeof(ConstRLEPayload::Segment) + sizeof(varpart_offset_t) + 5);
    }

    static size_t chunkSizeOffset()
    {
        return (sizeof(ConstRLEPayload::Header) + 2 * sizeof(ConstRLEPayload::Segment) + sizeof(varpart_offset_t) + 1);
    }

private:
    std::string                 _url;
    XMetadata::Format          _format;
    XMetadata::Compression     _compression;
    size_t                      _indexSplit;

    void checkIfSet(bool alreadySet, const char* kw)
    {
        if (alreadySet)
        {
            std::ostringstream error;
            error << "Illegal attempt to set " << kw << " multiple times";
            throw USER_EXCEPTION(SCIDB_SE_METADATA, SCIDB_LE_ILLEGAL_OPERATION) << error.str().c_str();
        }
    }

    void setParamFormat(std::vector<std::string> format)
    {
        if (format[0] == "arrow")
            _format = XMetadata::Format::ARROW;
        else
            throw USER_EXCEPTION(SCIDB_SE_METADATA, SCIDB_LE_ILLEGAL_OPERATION)
                << "format must be 'arrow'";
    }

    void setParamCompression(std::vector<std::string> compression)
    {
        if (compression[0] == "none")
            _compression = XMetadata::Compression::NONE;
        else if (compression[0] == "gzip")
            _compression = XMetadata::Compression::GZIP;
        else
            throw USER_EXCEPTION(SCIDB_SE_METADATA, SCIDB_LE_ILLEGAL_OPERATION)
                << "unsupported compression";
    }

    void setParamIndexSplit(std::vector<int64_t> indexSplit)
    {
        _indexSplit = indexSplit[0];
        if(_indexSplit < INDEX_SPLIT_MIN) {
            std::ostringstream err;
            err << "index_split must be at or above " << INDEX_SPLIT_MIN;
            throw USER_EXCEPTION(SCIDB_SE_METADATA, SCIDB_LE_ILLEGAL_OPERATION) << err.str();
        }
    }

    Parameter getKeywordParam(KeywordParameters const& kwp, const std::string& kw) const
    {
        auto const& kwPair = kwp.find(kw);
        return kwPair == kwp.end() ? Parameter() : kwPair->second;
    }

    std::string getParamContentString(Parameter& param)
    {
        std::string paramContent;

        if(param->getParamType() == PARAM_LOGICAL_EXPRESSION) {
            ParamType_t& paramExpr = reinterpret_cast<ParamType_t&>(param);
            paramContent = evaluate(paramExpr->getExpression(), TID_STRING).getString();
        } else {
            OperatorParamPhysicalExpression* exp =
                dynamic_cast<OperatorParamPhysicalExpression*>(param.get());
            SCIDB_ASSERT(exp != nullptr);
            paramContent = exp->getExpression()->evaluate().getString();
        }
        return paramContent;
    }

    int64_t getParamContentInt64(Parameter& param)
    {
        size_t paramContent;

        if(param->getParamType() == PARAM_LOGICAL_EXPRESSION) {
            ParamType_t& paramExpr = reinterpret_cast<ParamType_t&>(param);
            paramContent = evaluate(paramExpr->getExpression(), TID_INT64).getInt64();
        }
        else {
            OperatorParamPhysicalExpression* exp =
                dynamic_cast<OperatorParamPhysicalExpression*>(param.get());
            SCIDB_ASSERT(exp != nullptr);
            paramContent = exp->getExpression()->evaluate().getInt64();
            LOG4CXX_DEBUG(logger, "aio_save integer param is " << paramContent)

        }
        return paramContent;
    }

    bool setKeywordParamString(KeywordParameters const& kwParams,
                               const char* const kw,
                               void (XSaveSettings::* innersetter)(std::vector<std::string>) )
    {
        std::vector <std::string> paramContent;
        bool retSet = false;

        Parameter kwParam = getKeywordParam(kwParams, kw);
        if (kwParam) {
            if (kwParam->getParamType() == PARAM_NESTED) {
                auto group = dynamic_cast<OperatorParamNested*>(kwParam.get());
                Parameters& gParams = group->getParameters();
                for (size_t i = 0; i < gParams.size(); ++i) {
                    paramContent.push_back(getParamContentString(gParams[i]));
                }
            } else {
                paramContent.push_back(getParamContentString(kwParam));
            }
            (this->*innersetter)(paramContent);
            retSet = true;
        } else {
            LOG4CXX_DEBUG(logger, "XSAVE|findKeyword null: " << kw);
        }
        return retSet;
    }

    bool setKeywordParamInt64(KeywordParameters const& kwParams,
                              const char* const kw,
                              void (XSaveSettings::* innersetter)(std::vector<int64_t>) )
    {
        std::vector<int64_t> paramContent;
        size_t numParams;
        bool retSet = false;

        Parameter kwParam = getKeywordParam(kwParams, kw);
        if (kwParam) {
            if (kwParam->getParamType() == PARAM_NESTED) {
                auto group = dynamic_cast<OperatorParamNested*>(kwParam.get());
                Parameters& gParams = group->getParameters();
                numParams = gParams.size();
                for (size_t i = 0; i < numParams; ++i)
                    paramContent.push_back(getParamContentInt64(gParams[i]));
            }
            else
                paramContent.push_back(getParamContentInt64(kwParam));

            (this->*innersetter)(paramContent);
            retSet = true;
        }
        else
            LOG4CXX_DEBUG(logger, "aio_save findKeyword null: " << kw);

        return retSet;
    }

public:
    XSaveSettings(std::vector<std::shared_ptr<OperatorParam> > const& operatorParameters,
                   KeywordParameters const& kwParams,
                   bool logical,
                   std::shared_ptr<Query>& query):
                _format(XMetadata::Format::ARROW),
                _compression(XMetadata::Compression::NONE),
                _indexSplit(INDEX_SPLIT_DEFAULT)
    {
        if (operatorParameters.size() != 1)
            throw USER_EXCEPTION(SCIDB_SE_METADATA, SCIDB_LE_ILLEGAL_OPERATION) << "illegal number of parameters passed to xsave";
        std::shared_ptr<OperatorParam>const& param = operatorParameters[0];
        if (logical)
            _url = evaluate(((std::shared_ptr<OperatorParamLogicalExpression>&) param)->getExpression(), TID_STRING).getString();
        else
            _url = ((std::shared_ptr<OperatorParamPhysicalExpression>&) param)->getExpression()->evaluate().getString();

        setKeywordParamString(kwParams, KW_FORMAT,        &XSaveSettings::setParamFormat);
        setKeywordParamString(kwParams, KW_COMPRESSION,   &XSaveSettings::setParamCompression);
        setKeywordParamInt64( kwParams, KW_INDEX_SPLIT,   &XSaveSettings::setParamIndexSplit);
    }

    const std::string& getURL() const
    {
        return _url;
    }

    bool isArrowFormat() const
    {
        return _format == XMetadata::Format::ARROW;
    }

    XMetadata::Compression getCompression() const
    {
        return _compression;
    }

    size_t getIndexSplit() const
    {
        return _indexSplit;
    }
};

} // namespace scidb

#endif  // XSaveSettings
