# SciDB Input/Output Using External Storage

[![SciDB 19.11](https://img.shields.io/badge/SciDB-19.11-blue.svg)](https://forum.paradigm4.com/t/scidb-release-19-11/2411)
[![arrow 0.16.0](https://img.shields.io/badge/arrow-0.16.0-blue.svg)](https://arrow.apache.org/release/0.16.0.html)

This document contains installation and usage instructions of the
`bridge` SciDB plugin.

1. [Installation](#installation)
   1. [SciDB Plug-in](#scidb-plug-in)
   1. [Python Package](#python-package)
1. [AWS Configuration](#aws-configuration)
1. [Usage](#usage)

## Installation

### SciDB Plug-in

#### Using Extra SciDB Libs

Install extra-scidb-libs following the instructions
[here](https://paradigm4.github.io/extra-scidb-libs/).

#### From Source

#### AWS C++ SDK

1. Install the required packages:
   1. Ubuntu:
      ```
      apt-get install cmake libcurl4-openssl-dev
      ```
   1. RHEL/CentOS:
      ```
      yum install libcurl-devel
      yum install https://downloads.paradigm4.com/devtoolset-3/centos/7/sclo/x86_64/rh/devtoolset-3/scidb-devtoolset-3.noarch.rpm
      yum install cmake3 devtoolset-3-runtime devtoolset-3-toolchain
      scl enable devtoolset-3 bash
      ```
1. Download and unzip the SDK:
   ```
   wget --no-verbose --output-document - https://github.com/aws/aws-sdk-cpp/archive/1.8.3.tar.gz \
   | tar --extract --gzip --directory=.
   ```
1. Configure the SDK:
   ```
   > cd aws-sdk-cpp-1.8.3
   aws-sdk-cpp-1.8.3> mkdir build
   aws-sdk-cpp-1.8.3/build> cd build
   ```
   1. Ubuntu:
       ```
       aws-sdk-cpp-1.8.3/build> cmake ..            \
           -DBUILD_ONLY=s3                          \
           -DCMAKE_BUILD_TYPE=RelWithDebInfo        \
           -DBUILD_SHARED_LIBS=ON                   \
           -DCMAKE_INSTALL_PREFIX=/opt/aws
       ```
   1. RHEL/CentOS:
       ```
       aws-sdk-cpp-1.8.3/build> cmake3 ..                               \
           -DBUILD_ONLY=s3                                              \
           -DCMAKE_BUILD_TYPE=RelWithDebInfo                            \
           -DBUILD_SHARED_LIBS=ON                                       \
           -DCMAKE_INSTALL_PREFIX=/opt/aws                              \
           -DCMAKE_C_COMPILER=/opt/rh/devtoolset-3/root/usr/bin/gcc     \
           -DCMAKE_CXX_COMPILER=/opt/rh/devtoolset-3/root/usr/bin/g++
       ```
1. Compile and install the SDK:
   ```
   aws-sdk-cpp-1.8.3/build> make
   aws-sdk-cpp-1.8.3/build> make install
   ```
   The SDK will be installed in `/opt/aws`

#### Apache Arrow

1. Apache Arrow library version `0.16.0` is required. The easiest way
   to install it is by running:
   ```
   wget -O- https://paradigm4.github.io/extra-scidb-libs/install.sh \
   | sudo sh -s -- --only-prereq
   ```
1. Install Apache Arrow development library:
   1. Ubuntu
      ```
      apt-get install libarrow-dev=0.16.0-1
      ```
   1. RHEL/CentOS
      ```
      yum install arrow-devel-0.16.0
      ```

#### cURL (RHEL/CentOS ONLY)

Compile cURL with OpenSSL (instead of NSS):
```
> curl https://curl.haxx.se/download/curl-7.72.0.tar.gz | tar xz
> ./configure --prefix=/opt/curl
> make
> make install
```
More details: https://github.com/aws/aws-sdk-cpp/issues/1491


#### Compile and Load SciDB Plug-in

1. Checkout and compile the plug-in:
   ```
   > git clone https://github.com/Paradigm4/bridge.git
   bridge> make
   ```
1. Install in SciDB:
   ```
   bridge> cp libbridge.so /opt/scidb/19.11/lib/scidb/plugins
   ```
1. Restart SciDB and load the plug-in:
   ```
   scidbctl.py stop mydb
   scidbctl.py start mydb
   iquery --afl --query "load_library('bridge')"
   ```

### Python Package

```
pip install scidb-bridge
```

## AWS Configuration

1. AWS uses two separate filed to configure the S3 client. The
   `credentials` file is required and stores the AWS credentials for
   accessing S3, e.g.:
   ```
   > cat credentials
   [default]
   aws_access_key_id = ...
   aws_secret_access_key = ...
   ```
   The `config` file is optional and stores the region for the S3
   bucket. By default the `us-east-1` region is used, e.g.:
   ```
   > cat config
   [default]
   region = us-east-1
   ```
1. In SciDB installations these two files are located in
   `/home/scidb/.aws` directory, e.g.:
   ```
   > ls /home/scidb/.aws
   config
   credentials
   ```

Note: The credentials used need to have read/write permission to the
S3 bucket used.

## Usage

1. Save SciDB array in S3:
   ```
   > iquery --afl
   AFL% xsave(
          filter(
            apply(
              build(<v:int64>[i=0:9:0:5; j=10:19:0:5], j + i),
              w, double(v*v)),
            i >= 5 and w % 2 = 0),
          's3://p4tests/bridge/foo');
   {chunk_no,dest_instance_id,source_instance_id} val
   ```
   The SciDB array is saved in the `p4tests` bucket in the `foo` object.
1. Load SciDB array from S3 in Python:
   ```
   > python
   >>> import scidbbridge
   >>> ar = scidbbridge.Array('s3://p4tests/bridge/foo')

   >>> ar.metadata
   {'attribute':   'ALL',
    'compression': None,
    'format':      'arrow',
    'index_split': '100000',
    'namespace':   'public',
    'schema':      '<v:int64,w:double> [i=0:9:0:5; j=10:19:0:5]',
    'version':     '1'}

   >>> print(ar.schema)
   <v:int64,w:double> [i=0:9:0:5; j=10:19:0:5]

   >>> ar.read_index()
      i   j
   0  5  10
   1  5  15

   >>> ch = ar.get_chunk(5, 15)
   >>> ch.to_pandas()
        v      w  i   j
   0   20  400.0  5  15
   1   22  484.0  5  17
   2   24  576.0  5  19
   ...
   ```

Note: If using the file system for storage, make sure the storage is
shared across instances and that the path used by the non-admin SciDB
users is in `io-paths-list` in SciDB `config.ini`.

### Troubleshoot

It is common for S3 to return _Access Denied_ for non-obvious cases
like, for example, if the bucket specified does not exist. `xsave`
includes an extended error message for this type of errors which
include a link to a troubleshooting guide. E.g.:

```
> iquery -aq "xsave(build(<v:int64>[i=0:9], i), bucket_name:'foo', object_path:'bar')"
UserException in file: PhysicalXSave.cpp function: uploadS3 line: 372 instance: s0-i1 (1)
Error id: scidb::SCIDB_SE_ARRAY_WRITER::SCIDB_LE_UNKNOWN_ERROR
Error description: Error while saving array. Unknown error: Upload to
s3://foo/bar failed. Access Denied. See
https://aws.amazon.com/premiumsupport/knowledge-center/s3-troubleshoot-403/.
```
