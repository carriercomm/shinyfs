#pragma once
#ifndef ShinyMetaNode_H
#define ShinyMetaNode_H

#include <stdint.h>
#include <list>
#include "ShinyUser.h"
#include "ShinyGroup.h"


typedef uint64_t    inode_t;
#define INODE_MAX   UINT64_MAX


//I should make these belong to ShinyMetaNode
enum ShinyNodeType {
    SHINY_NODE_TYPE_NODE,
    SHINY_NODE_TYPE_FILE,
    SHINY_NODE_TYPE_DIR,
    SHINY_NODE_TYPE_ROOTDIR,
    NUM_SHINY_NODE_TYPES,
};

class ShinyFilesystem;
class ShinyMetaNode { 
public:
    //Generate a new node and create new inode (by asking ShinyMetaTree), etc.....
    ShinyMetaNode( ShinyFilesystem * fs, const char * newName );
    
    //Initialize this node, from serialized data.  Get amount of data read in via serializeLen() afterwards
    ShinyMetaNode( const char * serializedInput, ShinyFilesystem * fs );
    
    //Clean up (free name)
    ~ShinyMetaNode();
    
    //Name (filename, directory name, etc....)
    virtual void setName( const char * newName );
    virtual const char * getName();
    
    //Gets the absolute path to this node
    virtual const char * getPath();
    
    //Set new permissions for this node
    virtual void setPermissions( uint16_t newPermissions);
    virtual uint16_t getPermissions();
    
    //Get the unique id for this node
    virtual inode_t getInode();
    
    //Get the inode of the  parents of this node
    virtual const inode_t getParent( void );
    
    //Sets the parent of this node to newParent
    virtual void setParent( inode_t newParent );
        
    //Performs any necessary checks (e.g. directories check for multiple entries of the same node, etc...)
    virtual bool sanityCheck( void );
    
    //Returns the length of a serialization on this node
    virtual size_t serializedLen( void );
    
    //Serializes into the buffer [output]
    virtual void serialize( char * output );
    
    //Flushes things out to disk, clears temporary variables, etc... etc... etc...
    virtual void flush( void );
    
    //Returns the node type, e.g. if it's a file, directory, etc.
    virtual ShinyNodeType getNodeType( void );
protected:
    //Called by ShinyMetaNode() to load from a serialized string
    virtual void unserialize( const char * input );
    
    //Checks to make sure all of list are actually in fs.  If they aren't, removes them as a parent
    virtual bool check_existsInFs( std::list<inode_t> * list, const char * listName );
    
    //Checks to make sure our parent has us as a child
    virtual bool check_parentHasUsAsChild( void );
    
    //Checks to make sure we don't have any duplicates in a list of inodes
    virtual bool check_noDuplicates( std::list<inode_t> * list, const char * listName );
    
    //Pointer to fs so that we can traverse the tree if we must (and believe me, we must!)
    ShinyFilesystem * fs;
    
    //Parent of this inode
    inode_t parent;
    
    //Filename (+ extension)
    char * name;
    
    //This is the path of the node, destroyed everytime the parent is set, and cached
    //whenever getPath() is called.
    char * path;
    
    //Unique identifier for this node, whether it be directory or file
    inode_t inode;
    
    //file permissions
    uint16_t permissions;
    
    //User/Group IDs
    user_t uid;
    group_t gid;
    
    //These should all be maintained tracker-side
    uint64_t ctime;                 //Time created          (Set by tracker on initial client write())
    uint64_t atime;                 //Time last accessed    (Tracker notified by client on read())
    uint64_t mtime;                 //Time last modified    (Set by tracker on client write())
};

#endif