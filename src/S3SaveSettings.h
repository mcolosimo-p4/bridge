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

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <query/LogicalOperator.h>
#include <query/Query.h>
#include <query/Expression.h>
#include <util/PathUtils.h>

#ifndef S3SAVE_SETTINGS
#define S3SAVE_SETTINGS

using boost::algorithm::trim;
using boost::starts_with;
using boost::lexical_cast;
using boost::bad_lexical_cast;
using namespace std;

// Logger for operator. static to prevent visibility of variable outside of file
static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.operators.s3save"));

namespace scidb
{

static const char* const KW_BUCKET_NAME = "bucket_name";
static const char* const KW_OBJECT_PATH = "object_path";
static const char* const KW_FORMAT	= "format";

typedef std::shared_ptr<OperatorParamLogicalExpression> ParamType_t ;

class S3SaveSettings
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
    enum FormatType
    {
        ARROW  = 0
    };

    string			_bucketName;
    string			_objectPath;
    FormatType                  _format;

    void checkIfSet(bool alreadySet, const char* kw)
    {
        if (alreadySet)
        {
            ostringstream error;
            error << "illegal attempt to set " << kw << " multiple times";
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << error.str().c_str();
        }
    }

    void setParamBucketName(vector<string> bucketName)
    {
        if (_bucketName != "") {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set bucket name multiple times";
        }

        _bucketName = bucketName[0];
    }

    void setParamObjectPath(vector<string> objectPath)
    {
        if (_objectPath != "") {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set object path multiple times";
        }

        _objectPath = objectPath[0];
    }

    void setParamFormat(vector<string> format)
    {
        if(format[0] == "arrow")
        {
            _format = ARROW;
        }
        else
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "format must be 'arrow'";
        }
    }

    string getParamContentString(Parameter& param)
    {
        string paramContent;

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

    bool setKeywordParamString(KeywordParameters const& kwParams, const char* const kw, void (S3SaveSettings::* innersetter)(vector<string>) )
    {
        vector <string> paramContent;
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
            LOG4CXX_DEBUG(logger, "s3save findKeyword null: " << kw);
        }
        return retSet;
    }

    void setKeywordParamString(KeywordParameters const& kwParams, const char* const kw, bool& alreadySet, void (S3SaveSettings::* innersetter)(vector<string>) )
    {
        checkIfSet(alreadySet, kw);
        alreadySet = setKeywordParamString(kwParams, kw, innersetter);
    }

    Parameter getKeywordParam(KeywordParameters const& kwp, const std::string& kw) const
    {
        auto const& kwPair = kwp.find(kw);
        return kwPair == kwp.end() ? Parameter() : kwPair->second;
    }

public:
    S3SaveSettings(vector<shared_ptr<OperatorParam> > const& operatorParameters,
                   KeywordParameters const& kwParams,
                   bool logical,
                   shared_ptr<Query>& query):
                _bucketName(""),
                _objectPath(""),
                _format(ARROW)
    {
        bool  bucketNameSet  = false;
        bool  objectPathSet  = false;
        bool  formatSet      = false;

        setKeywordParamString(kwParams, KW_BUCKET_NAME, bucketNameSet, &S3SaveSettings::setParamBucketName);
        setKeywordParamString(kwParams, KW_OBJECT_PATH, objectPathSet, &S3SaveSettings::setParamObjectPath);
        setKeywordParamString(kwParams, KW_FORMAT, formatSet, &S3SaveSettings::setParamFormat);

        if(_bucketName.size() == 0)
        {
          throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << KW_BUCKET_NAME << " was not provided, or failed to parse";
        }

        if(_objectPath.size() == 0)
        {
          throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << KW_OBJECT_PATH << " was not provided, or failed to parse";
        }
    }

    bool isArrowFormat() const
    {
        return _format == ARROW;
    }

    string const& getBucketName() const
    {
        return _bucketName;
    }

    string const& getObjectPath() const
    {
        return _objectPath;
    }
};

}


#endif //S3SaveSettings
