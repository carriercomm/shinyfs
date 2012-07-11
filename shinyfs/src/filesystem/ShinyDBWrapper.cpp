//
//  ShinyDBWrapper.cpp
//  shinyfs
//
//  Created by Elliot Saba on 4/26/12.
//  Copyright (c) 2012 The Network Marines. All rights reserved.
//

#include "ShinyDBWrapper.h"
#include <base/Logger.h>

#define min( x, y ) ((x) > (y)? (y) : (x))

ShinyDBWrapper::ShinyDBWrapper( const char * path ) {
#ifdef KYOTOCABINET
    if( !this->db.open( path, kyotocabinet::PolyDB::OWRITER | kyotocabinet::PolyDB::OCREATE ) ) {
        ERROR( "Unable to open filecache in %s", filecache );
        throw "Unable to open filecache";
    }
#else
    leveldb::Options options;
    options.create_if_missing = true;
    this->status = leveldb::DB::Open( options, path, &this->db );
    if( !this->status.ok() ) {
        ERROR( "Unable to open filecache in %s", path );
        throw "Unable to open filecache";
    }
#endif
}

ShinyDBWrapper::~ShinyDBWrapper() {
#ifdef KYOTOCABINET
    this->db.close();
#else
    delete db;
#endif
}

int ShinyDBWrapper::get(const char *key, char *buffer, int maxsize) {
#ifdef KYOTOCABINET
    return this->db.get( key, strlen(key), buffer, maxsize );
#else
    std::string stupidDBBuffer;
    status = this->db->Get( leveldb::ReadOptions(), key, &stupidDBBuffer );
    memcpy( buffer, stupidDBBuffer.c_str(), min(maxsize, stupidDBBuffer.length()) );
    return status.ok() ? min(maxsize, stupidDBBuffer.length()) : -1;
#endif
}

int ShinyDBWrapper::put(const char *key, const char *buffer, int size) {
#ifdef KYOTOCABINET
    return this->db.set( key, strlen(key), buffer, maxsize );
#else
    status = this->db->Put( leveldb::WriteOptions(), key, buffer );
    return status.ok() ? size : -1;
#endif
}

bool ShinyDBWrapper::del(const char *key) {
#ifdef KYOTOCABINET
    return db.remove( key, strlen(key) );
#else
    status = db->Delete( leveldb::WriteOptions(), key );
    return status.ok();
#endif
}

const char * ShinyDBWrapper::getError() {
#ifdef KYOTOCABINET
    return this->db.error().name();
#else
    return this->status.ToString().c_str();
#endif
}