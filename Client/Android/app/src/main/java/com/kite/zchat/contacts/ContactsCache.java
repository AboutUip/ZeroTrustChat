package com.kite.zchat.contacts;

import androidx.annotation.Nullable;

import com.kite.zchat.main.FriendsListAdapter;
import com.kite.zchat.main.GroupsListAdapter;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * 通讯录列表内存缓存：按「服务器 + 当前用户」分桶，带 TTL；退出登录时 {@link #clear()}。
 * 需要强制实时刷新时可在业务侧调用 {@link #clear()} 或后续增加按类型失效。
 */
public final class ContactsCache {

    /** 默认 10 分钟；仅内存，进程结束即失效。 */
    private static final long DEFAULT_TTL_MS = 10 * 60 * 1000L;

    private static final Object LOCK = new Object();

    private static long ttlMs = DEFAULT_TTL_MS;

    private static String friendsKey;
    private static long friendsAtMs;
    private static ArrayList<FriendsListAdapter.ContactFriendItem> friends;

    private static String groupsKey;
    private static long groupsAtMs;
    private static ArrayList<GroupsListAdapter.ContactGroupItem> groups;

    private ContactsCache() {}

    public static String buildKey(String host, int port, String userIdHex32) {
        String h = host != null ? host : "";
        String u = userIdHex32 != null ? userIdHex32 : "";
        return h + ":" + port + ":" + u;
    }

    /** 仅测试或产品要求时可调整。 */
    public static void setTtlMs(long ms) {
        synchronized (LOCK) {
            ttlMs = Math.max(5_000L, ms);
        }
    }

    public static void clear() {
        synchronized (LOCK) {
            friendsKey = null;
            friends = null;
            friendsAtMs = 0L;
            groupsKey = null;
            groups = null;
            groupsAtMs = 0L;
        }
    }

    @Nullable
    public static List<FriendsListAdapter.ContactFriendItem> friendsIfFresh(String key) {
        synchronized (LOCK) {
            if (key == null || friendsKey == null || !friendsKey.equals(key) || friends == null) {
                return null;
            }
            if (friends.isEmpty()) {
                // 空列表不命中缓存：避免「对方刚同意」后仍显示旧缓存（无好友/无头像）。
                return null;
            }
            if (System.currentTimeMillis() - friendsAtMs > ttlMs) {
                return null;
            }
            ArrayList<FriendsListAdapter.ContactFriendItem> out = new ArrayList<>(friends.size());
            for (FriendsListAdapter.ContactFriendItem it : friends) {
                out.add(copyFriend(it));
            }
            return out;
        }
    }

    public static void putFriends(String key, List<FriendsListAdapter.ContactFriendItem> items) {
        synchronized (LOCK) {
            friendsKey = key;
            friendsAtMs = System.currentTimeMillis();
            friends = new ArrayList<>();
            if (items != null) {
                for (FriendsListAdapter.ContactFriendItem it : items) {
                    friends.add(copyFriend(it));
                }
            }
        }
    }

    @Nullable
    public static List<GroupsListAdapter.ContactGroupItem> groupsIfFresh(String key) {
        synchronized (LOCK) {
            if (key == null || groupsKey == null || !groupsKey.equals(key) || groups == null) {
                return null;
            }
            if (System.currentTimeMillis() - groupsAtMs > ttlMs) {
                return null;
            }
            ArrayList<GroupsListAdapter.ContactGroupItem> out = new ArrayList<>(groups.size());
            for (GroupsListAdapter.ContactGroupItem it : groups) {
                out.add(
                        new GroupsListAdapter.ContactGroupItem(
                                it.groupIdHex, it.name, it.memberCount));
            }
            return out;
        }
    }

    public static void putGroups(String key, List<GroupsListAdapter.ContactGroupItem> items) {
        synchronized (LOCK) {
            groupsKey = key;
            groupsAtMs = System.currentTimeMillis();
            groups = new ArrayList<>();
            if (items != null) {
                for (GroupsListAdapter.ContactGroupItem it : items) {
                    groups.add(
                            new GroupsListAdapter.ContactGroupItem(
                                    it.groupIdHex, it.name, it.memberCount));
                }
            }
        }
    }

    private static FriendsListAdapter.ContactFriendItem copyFriend(
            FriendsListAdapter.ContactFriendItem it) {
        byte[] av = it.avatarBytes;
        if (av != null) {
            av = Arrays.copyOf(av, av.length);
        }
        return new FriendsListAdapter.ContactFriendItem(it.userIdHex, it.displayName, av);
    }
}
