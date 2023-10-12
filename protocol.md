## Infinote Protocol Specification

## Terminology

Before starting, we recommend to get familiar with some of the terminology that is used in this protocol specification.

* Host: In fact, a host is referred to an instance of the program implementing the Infinote protocol. If there are multiple instances running on the same computer, then these are referred to as multiple hosts, for simplicity.
* Directory: Each Infinote server maintains an hierarchical tree of documents, called its *Directory*.
* Node: An entry in a server's directory. A node can be either a subdirectory node, or a session node. A subdirectory node can contain more nodes, whereas a session node is a leaf of the tree.
* Exploration: A client can *explore* a subdirectory node of a server. This involves the server sending the subdirectory's contents to the client and notifying it of changes, in case nodes are added or removed in that subdirectory.
* Session: For each session node, there is a corresponding *Session*. A session allows multiple hosts to collaboratively modify a document.
* Buffer: The contents of a session's document. For example for a session which allows to modify a text document, the buffer contains the actual text.
* Synchronization: The process of copying a session's state from one host to another is called *Synchronization*.
* Subscription: A host can *subscribe* to a session node. If it is subscribed, it is notified of any changes to the session by other (subscribed) hosts.
* User: When a host is subscribed, it can join one or more *Users* into the session. A user can make changes to it. So if a host is subscribed but has not joined a user, then it can only watch others modifying the document but not edit it itself. It needs to join a user to do so.
* Request: A host asking one or more other hosts to do something, then we call that message a *Request*.

## Communication Groups

All communication in Infinote is organized in *communication groups*. A communication group is a group of hosts which are able to send messages to each other or to the whole group. The idea behind this concept is to decouple the messages defined by the protocol from the transport layer used to transmit the messages between hosts. The core Infinote protocol just specifies a set of XML messages that need to be understood by hosts. How these messages are transmitted is up to the communication group, or more specifically, the *communication method* of the group.

Before elaborating more on this, let us introduce the concept of a *network* in Infinote terminology. A network is an object which connects multiple hosts and allows them to exchange XML messages. It is expected, that each host has a unique identifier on a given network. For example, TCP/IP can serve as a network where an IP address/port number pair can be used as a host's ID. Another example can be a Jabber network where an account's JID can be used as the ID. It is also expected that hosts on different networks can not talk to each other, however a host may be present on multiple networks.

Each communication group is uniquely identified by a name and the network ID of the publisher of the group. This means that every group is published by a certain host. The group can be published on all the networks that host is on. In this case, the publisher is responsible for relaying messages between networks. This also means that if the publisher goes down, there is no way for group members in a certain network to still reach the members in a different network: The group is "split" into a different group on each network. There is currently no possibility for another group member to take over the responsibility to relay messages between networks if it happens to be on more than one network, but such functionality can be added later. Note that also, when the publisher goes down, the group can still continue to exist on a single network only if the communication method allows. This is why, when publishing a new group, a host should always make sure to choose a unique name for the new group. Even if it dropped the group earlier there might still be other hosts which continue to use it. There is one exception to this rule which affects the initial group, see below in the corresponding section for details.

Finally, the communication within a group on a network is controlled by a communication method. All group members need to use the same communication method, so when joining a group they need to know what method is being used. The method defines exactly how XML messages are transferred via the network. However, a group may use different methods on different networks. Some methods may not even be available on a certain network. All it needs to do is to fulfill the following requirements:

* Reliability: It needs to make sure that, when a host is sending a message, the message will arrive at the destination. If the underlying transport (such as TCP or UDP) does not guarantee reliability, then it needs to take care of that itself by acknowledging and resending messages if needed.
* Ordering: It needs to guarantee that messages arrive in the same order they have been sent. Again, if this is not already guaranteed by whatever transport the method uses it needs to make sure itself that messages are reported to the upper layer in order, for example, by adding sequence numbers to messages and holding back messages if an earlier sent message has not yet arrived.
* Multicasting: It needs to be able to send messages both to single group members to which a host has a direct connection and to all of the group. This is called the scope of a message, and it can be either point-to-point (to a single host only) or group (to all group members).

Given these preconditions the higher level of the Infinote protocol can be specified in a general way, not depending on how the messages are actually transported. This provides a great flexibility and applicability.

Upon reception of a message, the message is processed and the so-called scope of the message is decided. The scope of the message can be either "point-to-point", meaning the message is sent from one group member to another one, or "group", in which case the message is sent to the whole group. If the message has scope "group", then the communication method needs to make sure that all group members receive the message, for example, if the host is the publisher of the group, then it needs to relay the message to group members of other networks then the one of the connection from which the message comes from. It is suggested that, when an error occurs during processing of a message, then the message is only treated as a "point-to-point" one, even if it would have "group" scope otherwise.

### Defined networks

The following networks are currently defined:

* **tcp/ip**: Connections are established via TCP/IP, then a XMPP stream is opened, as described by [RFC 3920](http://www.ietf.org/rfc/rfc3920.txt). After that, infinote's XML messages are exchanged as they are. IP address and port number in standard dots notation, i.e. 127.0.0.1:6523 for IPv4 or [::1]:6523 for IPv6 are used as host IDs. Note that actually this is not unique since, if hosts are behind a NAT, then hosts from outside might see a different IP address than hosts from within. This is why only the "central" method (see below) is currently supported for tcp/ip connections.

Another possible network, which could be added later, would be communication via a Jabber server.

### Communication Methods

The following communication methods are currently defined. Each communication method is identified by its name. A communication method can be defined for all networks, or for a single network only. In the latter case, it's name must be qualified with the network name in the form "network-name::method-name".

#### central

The central method is a very simple communication method which is supported for all networks. Each host maintains only a single connection to the publisher, and the publisher maintains a connection to each other host. Hosts can only send messages to the publisher, and if the message is a group message, then the publisher relays the message to all other hosts. Therefore, if the publishing host goes down, no communication is possible anymore within the group.

An XML message being sent with the central method is sent as a child of the top-level `<group/>` message. On the top-level, only `<group>` are allowed. They have two attributes namely "name" and "publisher", which identify the group the message belongs to. "name" contains the group name and "publisher" the network ID of the host that published the group. "publisher" can also be one of "me" or "you" in which case the sending or receiving host is referred to as the group publisher, respectively. If no publisher is given, then "me" is assumed.

Example 1:

```xml
<group name="InfDirectory" publisher="you">
  <more-content />
</group>
```

Example 2:

```xml
<group name="InfDirectory" publisher="84.241.121.83:6523">
  <more-content />
</group>
```

Other communication methods can be added later. Examples are "peer-to-peer" (either in a way where each client is guaranteed to be able to connect to each other client, which is a reasonable assumption on a Jabber network, or in a "failsafe" way in which it is not guaranteed that each host can directly connect to each other host, such as in a TCP/IP environment with NAT being involved), or "jabber::groupchat" where group messages are relayed in a jabber multi user chatroom. "udp::multicast" might be another interesting option.

### Initial group

After connecting to a server, each client has implicitly joined a group with name "InfDirectory", published by the server. The group always uses the "central" method and is thus available on all networks. Also, since it uses the "central" method, the group does no longer exist once the server goes down. Therefore it is safe to have the restarted server's initial group also named "InfDirectory".

### Enhanced Child Text

Some XML nodes allow child text which contains `<uchar codepoint="uuuu" />` children, with `uuuu` being a unicode character point in decimal notation. This is to be interpreted as if the child text contained the given unicode character at the position of the `<uchar />` node. This allows transmission of characters that would otherwise be invalid XML, such as null bytes. The protocol specification explicitly stated whether a message allows such *enhanced* child text or not.

## Directory

The directory is the central entry point for all Infinote communication. As pointed out in the previous section, after connection establishment the initiating host (in the following called the "client") implicitely joins the "InfDirectory" group published by the accepting host (the "server"). The group uses the "central" method for communication. Note that even though we are talking about client and server here, and that even though most messages in the InfDirectory group are of request-reply type this does not mean that the Infinote protocol is solely client-server based. The directory is the entity which contains and distributes information about documents available at a host and the messages presented in this section specify how they are queried. Once subscribed to a session the communication can be performed by any available communication method supported on the underlying network and does not have to be of client-server type.

Note that what documents are available in a directory is totally up to its host. For example, a dedicated Infinote server could publish a directory on its file system whereas a text editor could publish all currently open documents. A host can also make available sessions published by other hosts that it is subscribed to.

### Node types

The directory represents a hierarchical tree of documents. Each node in the directory has a unique (for that directory) ID number. A node can either be a subdirectory node which in turn can contain other nodes (children), or it can be a document node which contains a document and which can be used as an entry point to edit that document collaboratively. Every node also is assigned a type. If the type is "InfSubdirectory" then it is a subdirectory node (as explained above), otherwise it is a document node hosting a document of the given type. For example, "InfChat" is for chat sessions and "InfText" for plain text documents. The welcome message (see below) specifies what type of sessions a server can handle.

There is one root node in the directory. All other nodes are children of that root node, either directly or indirectly (that is they are children of another node which in turn is a child of the root node, directly or indirectly). The root node of a directory has always type "InfSubdirectory" and ID 0 (zero).

### Exploration and Subscription

Nodes of type "InfSubdirectory" can be "explored" by clients. Exploring a node means sending the content of the node to the client and also notifying it when there is any change to the subdirectory (such as nodes being added or removed later).

Clients can "subscribe" to nodes of other types. When a client is subscribed to a node then it is sent a copy of the full node content (a process called "synchronization") and then notified of any changes to it. Synchronization and subscription take place in a different group than "InfDirectory" so that other communication methods can be used and so that communication in InfDirectory is not blocked by a potential lengthy synchronization. How exactly the initial node content and any further changes are transmitted depends on the type of the node. The common messages that are used for all node types are described in the sections Synchronization and Session, respectively. Nodes of type "InfChat" are documented in Chat Session and nodes of type "InfText" are described in Text Session.

### Sequence numbers and sequence IDs

In the welcome message (see below) each client is assigned a unique sequence ID, an integral positive number. For each request a client sends to the server it can optionally specify a sequence number. The server's reply to the request then contains both the sequence ID of the client initiating the request and the sequence number. This allows clients to perform multiple requests without waiting for a reply from the server. The sequence ID is also included since the reply for some requests are sent to the whole group (for example if a new node has been created in response to a client request).

### Messages

**TODO**

## Synchronization

**TODO:** How session contents are copied between hosts.

## Session

**TODO:** Messages commonly understood by all types of sessions.

## Chat Session

**TODO:** How chat messages are exchanged.

## Text Session

**TODO:** How plain text editing works.

## Error Codes

**TODO:** Error codes and domains.
