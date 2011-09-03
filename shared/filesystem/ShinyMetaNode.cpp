	//
//  ShinyMetaNode.cpp
//  ShinyMetafs-tracker
//
//  Created by Elliot Nabil Saba on 5/25/11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#include "ShinyMetaNode.h"
#include "ShinyMetaDir.h"
#include "ShinyFilesystem.h"
#include <base/Logger.h>
#include <sys/stat.h>

ShinyMetaNode::ShinyMetaNode(ShinyFilesystem * fs, const char * newName) {
    //Save pointer to fs
    this->fs = fs;
    
    //Generate a new inode for this node
    this->inode = fs->genNewInode();
    
    //Add this Node into the inode map
    this->fs->nodes[this->inode] = this;
    
    //Set name
    this->name = NULL;
    this->setName( newName );
    
    //Force ourselves to recalculate path next time someone asks for it
    this->path = NULL;
    
    //Set parent to NULL for now
    this->parent = NULL;
    
    //It's HAMMAH TIME!!!
    this->ctime = this->atime = this->mtime = time(NULL);
    
    this->setPermissions( S_IRWXU | S_IRWXG | S_IRWXO );   //aka rwxrwxrwx
    
    //Set these to nothing right now
    LOG( "need to init uid/gid!\n" );
    this->uid = this->gid = NULL;
}

ShinyMetaNode::ShinyMetaNode( const char * serializedInput, ShinyFilesystem * fs ) : fs( fs ), path( NULL ) {
    this->unserialize(serializedInput);
    
    //Add this Node into the inode map
    this->fs->nodes[this->inode] = this;
}

ShinyMetaNode::~ShinyMetaNode() {
    if( this->name )
        delete( this->name );
    if( this->path )
        delete( this->path );
    
    //Remove myself from the map
    this->fs->nodes.erase(this->inode);
    
    //Remove myself from my parent
    ShinyMetaDir * parentDir = (ShinyMetaDir *) this->fs->findNode( this->parent );
    if( parentDir )
        parentDir->delNode( this );
}

inode_t ShinyMetaNode::getInode() {
    return this->inode;
}

const inode_t ShinyMetaNode::getParent( void ) {
    return this->parent;
}

void ShinyMetaNode::setParent( inode_t newParent ) {
    this->parent = newParent;
    if( this->path ) {
        delete( this->path );
        this->path = NULL;
    }
}


void ShinyMetaNode::setName( const char * newName ) {
    if( this->name )
        delete( this->name );
    
    this->name = new char[strlen(newName)+1];
    strcpy( this->name, newName );
    
    fs->setDirty();
}

const char * ShinyMetaNode::getName( void ) {
    return (const char *)this->name;
}

const char * ShinyMetaNode::getPath() {
    //If we've cached the result from a previous call, then just return that!
    if( this->path )
        return this->path;
    //Otherwise, let's calculate it!
    
    //First, get the length:
    uint64_t len = 0;

    //We'll store all the paths in here so we don't have to traverse the tree multiple times
    std::list<const char *> paths;
    paths.push_front( this->getName() );
    len += strlen( this->getName() );
    
    ShinyMetaNode * currNode = this;
    ShinyMetaNode * root = fs->findNode("/");
    while( currNode != (ShinyMetaNode *) root) {
        //Move up in the chain of parents
        ShinyMetaNode * nextNode = fs->findNode( currNode->getParent() );
        
        //If our parental chain is broken, just return ?/name
        if( !nextNode )
            return this->getName();
        else if( nextNode != (ShinyMetaNode *)root ) {
            //Push this parent's path onto the list, and add its length to len
            paths.push_front( nextNode->getName() );
            len += strlen( nextNode->getName() );
        }
        currNode = nextNode;
    }
    
    //Add 1 for each slash in front of each path
    len += paths.size();
    
    //Add another 1 for the NULL character
    this->path = new char[len+1];
    path[len] = NULL;

    len = 0;
    for( std::list<const char *>::iterator itty = paths.begin(); itty != paths.end(); ++itty ) {
        path[len++] = '/';
        strcpy( path + len, *itty );
        len += strlen( *itty );
    }
    return path;
}

void ShinyMetaNode::setPermissions( uint16_t newPermissions ) {
    this->permissions = newPermissions;
    fs->setDirty();
}

uint16_t ShinyMetaNode::getPermissions() {
    return this->permissions;
}

bool ShinyMetaNode::check_existsInFs( std::list<inode_t> * list, const char * listName ) {
    bool retVal = true;
    std::list<inode_t>::iterator itty = list->begin();
    while( itty != list->end() ) {
        if( !this->fs->findNode( *itty ) ) {
            WARN( "Orphaned member of %s pointing to [%llu], belonging to %s [%llu]", listName, *itty, this->getPath(), this->inode );
            std::list<inode_t>::iterator delItty = itty++;
            list->erase( delItty );
            retVal = false;
        } else
            ++itty;
    }
    return retVal;
}

bool ShinyMetaNode::check_parentHasUsAsChild( void ) {
    //First, check to make sure the parent node exists.....
    ShinyMetaDir * parentNode = (ShinyMetaDir *)fs->findNode( this->parent );
    if( parent ) {
        //Iterate through all children, looking for us
        const std::list<inode_t> * children = parentNode->getListing();
        for( std::list<inode_t>::const_iterator cItty = children->begin(); cItty != children->end(); ++cItty ) {
            if( *cItty == this->inode )
                return true;
        }
        //If we can't find ourselves, FIX IT!
        WARN( "Parent %s [%llu] did not point to child %s [%llu], when it should have! Fixing...\n", parentNode->getPath(), parentNode->getInode(), this->getPath(), this->inode );
        parentNode->addNode( this );
        
        //check to make sure we actually _could_ do that
        for( std::list<inode_t>::const_iterator cItty = children->begin(); cItty != children->end(); ++cItty ) {
            if( *cItty == this->inode )
                return true;
        }
        WARN( "Couldn't fix! Setting our parent to NULL so we get garbage-collected..." );
        this->parent = NULL;
        return false;
    } else {
        ERROR( "parent [%d] of node %s [%d] could not be found in fs!", this->parent, this->name, this->inode );
        return false;
    }
    return true;
}

bool ShinyMetaNode::check_noDuplicates( std::list<inode_t> * list, const char * listName ) {
    bool retVal = true;
    std::list<inode_t>::iterator itty = list->begin();
    std::list<inode_t>::iterator last_iterator = itty++;
    while( itty != list->end() ) {
        if( *itty == *last_iterator ) {
            ShinyMetaNode * listNode = this->fs->findNode( *itty );
            if( listNode ) {
                WARN( "Warning, %s for node %s [%llu] has duplicate entries for %s [%llu] in it!", listName, this->getPath(), this->inode, listNode->getPath(), *itty );
            } else {
                WARN( "Warning, %s for node %s [%llu] has duplicate entries for a child that could not be found [%llu]! Erasing...", listName, this->getPath(), this->inode, *itty );
            }
            
            list->erase( last_iterator );
            last_iterator = itty++;
            retVal = false;
        } else
            ++itty;
    }
    return retVal;
}

bool ShinyMetaNode::sanityCheck() {
    bool retVal = true;
    if( permissions == 0 ) {
        WARN( "permissions == 0 for %s", this->getPath() );
        retVal = false;
    }
    
    retVal &= this->check_parentHasUsAsChild();
    return retVal;
}

size_t ShinyMetaNode::serializedLen() {
    //First, the size of the inode pointers
    size_t len = sizeof(inode);

    //Time markers
    len += sizeof(ctime) + sizeof(atime) + sizeof(mtime);
    
    //Then, permissions and user/group ids
    len += sizeof(uid) + sizeof(gid) + sizeof(permissions);
    
    //parent inode
    len += sizeof(parent);
    
    //Finally, filename
    len += strlen(name) + 1;
    return len;
}

/* Serialization order is as follows:
 [inode]         - uint64_t
 [ctime]         - uint64_t
 [atime]         - uint64_t
 [mtime]         - uint64_t
 [uid]           - uint32_t
 [gid]           - uint32_t
 [permissions]   - uint16_t
 [parents]       - uint64_t + uint64_t * num_parents
 [name]          - char * (\0 terminated)
*/

#define write_and_increment( value, type )    *((type *)output) = value; output += sizeof(type)

void ShinyMetaNode::serialize(char * output) {
    write_and_increment( this->inode, inode_t );
    write_and_increment( this->ctime, uint64_t );
    write_and_increment( this->atime, uint64_t );
    write_and_increment( this->mtime, uint64_t );
    write_and_increment( this->uid, user_t );
    write_and_increment( this->gid, group_t );
    write_and_increment( this->permissions, uint16_t );
    write_and_increment( this->parent, inode_t );
    
    //Finally, write out a \0-terminated string of the filename
    strcpy( output, this->name );
}

#define read_and_increment( value, type )   value = *((type *)input); input += sizeof(type)

void ShinyMetaNode::unserialize( const char * input ) {
    read_and_increment( this->inode, inode_t );
    read_and_increment( this->ctime, uint64_t );
    read_and_increment( this->atime, uint64_t );
    read_and_increment( this->mtime, uint64_t );
    read_and_increment( this->uid, uint32_t );
    read_and_increment( this->gid, uint32_t );
    read_and_increment( this->permissions, uint16_t );
    read_and_increment( this->parent, inode_t );
    
    this->name = new char[strlen(input) + 1];
    strcpy( this->name, input );
    
    if( this->path ) {
        delete( this->path );
        this->path = NULL;
    }
}

void ShinyMetaNode::flush( void ) {
    //Just don't do anything here
}

ShinyNodeType ShinyMetaNode::getNodeType( void ) {
    return SHINY_NODE_TYPE_NODE;
}