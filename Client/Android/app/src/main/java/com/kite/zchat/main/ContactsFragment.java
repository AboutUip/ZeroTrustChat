package com.kite.zchat.main;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.viewpager2.widget.ViewPager2;

import com.google.android.material.tabs.TabLayout;
import com.google.android.material.tabs.TabLayoutMediator;

import com.kite.zchat.R;

/** 通讯录：好友 / 群聊 分栏，进入后子页各自拉取列表。 */
public final class ContactsFragment extends Fragment {

    private static final String ARG_HOST = "host";
    private static final String ARG_PORT = "port";

    public static ContactsFragment newInstance(@Nullable String host, int port) {
        ContactsFragment f = new ContactsFragment();
        Bundle b = new Bundle();
        if (host != null) {
            b.putString(ARG_HOST, host);
        }
        b.putInt(ARG_PORT, port);
        f.setArguments(b);
        return f;
    }

    @Nullable
    @Override
    public View onCreateView(
            @NonNull LayoutInflater inflater,
            @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_contacts, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        TabLayout tabs = view.findViewById(R.id.contactsTabLayout);
        ViewPager2 pager = view.findViewById(R.id.contactsViewPager);
        pager.setAdapter(new ContactsPagerAdapter(this));
        pager.setOffscreenPageLimit(1);
        new TabLayoutMediator(
                        tabs,
                        pager,
                        (tab, position) ->
                                tab.setText(
                                        position == 0
                                                ? R.string.contacts_tab_friends
                                                : R.string.contacts_tab_groups))
                .attach();
    }
}
