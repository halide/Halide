import * as React from "react";

import type { Profile } from "@/types/profile";

const Profile = React.createContext<Profile>({
  pipelines: [],
});

export const ProfileContextProvider = Profile.Provider;
export const useProfileContext = () => React.useContext(Profile);
