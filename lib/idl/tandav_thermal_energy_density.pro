function tandav_thermal_energy_density, rho, T, xH, h=h, z=z, TisU=TisU

    @set_cgs

      if not (exist(rho) or exist(T) or exist(xH)) then begin
        print, "tandav_thermal_energy_density, rho, T, xH, h=h, z=z, TisU=TisU"
        return, -1
    end

    if exist(TisU) then $
        T = tandav_U2T(T)

    umol = 4./(5.*xH+3.)

    return, tandav_density(rho, h=h, z=z) / (mp*umol) * k_Boltz * T
end
